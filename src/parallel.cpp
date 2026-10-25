#include "wyrm/parallel.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace wyrm {

namespace {

// Слот воркера в общей памяти: пишет только он, родитель читает для живого вывода. Без блокировок:
// это счётчики для отображения, точные итоги приходят в отчёте воркера при завершении.
struct Slot {
    volatile long runs;
    volatile long crashes;
    volatile size_t corpus;
    volatile size_t edges;
};

volatile sig_atomic_t g_child_stop = 0;
void on_child_signal(int) { g_child_stop = 1; }

std::string flat(std::string s) {
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

void write_all(int fd, const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = write(fd, s.data() + off, s.size() - off);
        if (n <= 0) return;
        off += static_cast<size_t>(n);
    }
}

[[noreturn]] void worker_main(Options opts, Slot* slot, int report_fd) {
    signal(SIGINT, on_child_signal);
    signal(SIGTERM, on_child_signal);
    std::ostringstream out;
    int code = 0;
    try {
        Fuzzer fuzzer(opts);
        fuzzer.resume();
        std::string reason = fuzzer.run(
            [&](const Stats& s) {
                slot->runs = s.runs;
                slot->crashes = s.crashes_found;
                slot->corpus = s.corpus_size;
                slot->edges = s.edges_covered;
            },
            [] { return g_child_stop != 0; });
        const Stats& s = fuzzer.stats();
        out << "STATS\t" << s.runs << '\t' << s.crashes_found << '\t' << s.corpus_size << '\t' << s.edges_covered << '\t' << s.crashes_total << '\n';
        for (const auto& c : fuzzer.crashes())
            out << "CRASH\t" << c.file << '\t' << flat(c.reason) << '\t' << static_cast<int>(c.status) << '\t' << c.signal << '\t'
                << (c.reproducible ? 1 : 0) << '\t' << c.duplicates << '\t' << flat(c.signature) << '\n';
        out << "REASON\t" << reason << '\n';
    } catch (const std::exception& e) {
        out << "ERROR\t" << flat(e.what()) << '\n';
        code = 2;
    }
    write_all(report_fd, out.str());
    close(report_fd);
    _exit(code);
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        size_t pos = line.find('\t', start);
        parts.push_back(line.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return parts;
}

}  // namespace

ParallelResult run_parallel(const Options& opts, const std::string& seed_dir, const std::function<void(const Stats&)>& progress,
                            const std::function<bool()>& should_stop) {
    const unsigned jobs = opts.jobs < 1 ? 1 : opts.jobs;
    ParallelResult result;
    result.workers = jobs;

    // Начальные сиды кладёт один процесс до старта воркеров: иначе каждый воркер записал бы свою копию.
    if (!seed_dir.empty()) {
        Options so = opts;
        so.worker_tag.clear();
        so.persistent = false;
        Fuzzer seeder(so);
        seeder.resume();
        seeder.seed_from_dir(seed_dir);
        result.crashes = seeder.crashes();
        result.stats.crashes_total = seeder.stats().crashes_total;
    }

    void* mem = mmap(nullptr, sizeof(Slot) * jobs, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) throw std::runtime_error("mmap для счётчиков воркеров не удался");
    auto* slots = static_cast<Slot*>(mem);
    std::memset(mem, 0, sizeof(Slot) * jobs);

    struct Child {
        pid_t pid = -1;
        int report_fd = -1;
        bool done = false;
        std::string report;
    };
    std::vector<Child> children(jobs);

    for (unsigned i = 0; i < jobs; ++i) {
        int fds[2];
        if (pipe(fds) != 0) throw std::runtime_error("pipe() не удался");
        Options wo = opts;
        wo.worker_tag = "w" + std::to_string(i) + "-";
        wo.seed = opts.seed ? opts.seed + i : 0;
        if (opts.max_runs >= 0) wo.max_runs = (opts.max_runs + jobs - 1) / jobs;
        pid_t pid = fork();
        if (pid < 0) throw std::runtime_error("fork() не удался");
        if (pid == 0) {
            close(fds[0]);
            for (auto& c : children)
                if (c.report_fd >= 0) close(c.report_fd);  // чужие каналы воркеру не нужны
            worker_main(wo, &slots[i], fds[1]);
        }
        close(fds[1]);
        fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        children[i].pid = pid;
        children[i].report_fd = fds[0];
    }

    const auto start = std::chrono::steady_clock::now();
    bool stop_sent = false;
    unsigned alive = jobs;
    while (alive > 0) {
        if (!stop_sent && should_stop && should_stop()) {
            for (auto& c : children)
                if (!c.done) kill(c.pid, SIGTERM);
            stop_sent = true;
        }
        // Читаем отчёты по мере поступления: иначе воркер с большим отчётом завис бы на записи в полный канал.
        std::vector<pollfd> pfds;
        for (auto& c : children)
            if (c.report_fd >= 0) pfds.push_back({c.report_fd, POLLIN, 0});
        if (!pfds.empty()) poll(pfds.data(), pfds.size(), 100);
        else usleep(100000);
        for (auto& c : children) {
            if (c.report_fd < 0) continue;
            char buf[4096];
            ssize_t n;
            while ((n = read(c.report_fd, buf, sizeof(buf))) > 0) c.report.append(buf, static_cast<size_t>(n));
            if (n == 0) {
                close(c.report_fd);
                c.report_fd = -1;
            }
        }
        for (auto& c : children) {
            if (c.done) continue;
            int status;
            if (waitpid(c.pid, &status, WNOHANG) == c.pid) {
                c.done = true;
                --alive;
            }
        }
        if (progress) {
            Stats s;
            double eps = 0;
            for (unsigned i = 0; i < jobs; ++i) {
                s.runs += slots[i].runs;
                s.crashes_found += slots[i].crashes;  // по воркерам: повтор того же бага у разных воркеров учтётся в итоге
                s.corpus_size = std::max<size_t>(s.corpus_size, static_cast<size_t>(slots[i].corpus));
                s.edges_covered = std::max<size_t>(s.edges_covered, static_cast<size_t>(slots[i].edges));
            }
            s.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            eps = s.elapsed > 0 ? static_cast<double>(s.runs) / s.elapsed : 0.0;
            s.exec_per_sec = eps;
            progress(s);
        }
    }
    // Хвосты отчётов, дошедшие после завершения процессов.
    for (auto& c : children) {
        if (c.report_fd < 0) continue;
        char buf[4096];
        ssize_t n;
        while ((n = read(c.report_fd, buf, sizeof(buf))) > 0) c.report.append(buf, static_cast<size_t>(n));
        close(c.report_fd);
    }
    munmap(mem, sizeof(Slot) * jobs);

    std::string error;
    bool first_reason = true;
    for (auto& c : children) {
        std::istringstream in(c.report);
        std::string line;
        while (std::getline(in, line)) {
            auto f = split_tabs(line);
            if (f[0] == "STATS" && f.size() >= 5) {
                result.stats.runs += std::stol(f[1]);
                if (f.size() >= 6) result.stats.crashes_total += std::stol(f[5]);
                result.stats.corpus_size = std::max<size_t>(result.stats.corpus_size, static_cast<size_t>(std::stoul(f[3])));
                result.stats.edges_covered = std::max<size_t>(result.stats.edges_covered, static_cast<size_t>(std::stoul(f[4])));
            } else if (f[0] == "CRASH" && f.size() >= 6) {
                CrashRecord rec;
                rec.file = f[1];
                rec.reason = f[2];
                rec.status = static_cast<RunResult::Status>(std::stoi(f[3]));
                rec.signal = std::stoi(f[4]);
                rec.reproducible = f[5] == "1";
                if (f.size() >= 8) {
                    rec.duplicates = std::stol(f[6]);
                    rec.signature = f[7];
                }
                auto same = std::find_if(result.crashes.begin(), result.crashes.end(), [&](const CrashRecord& c) {
                    return !rec.signature.empty() && c.signature == rec.signature;
                });
                if (same != result.crashes.end()) {
                    same->duplicates += rec.duplicates + 1;  // тот же баг нашёл другой воркер
                    if (!rec.reproducible) same->reproducible = false;
                } else {
                    result.crashes.push_back(std::move(rec));
                }
            } else if (f[0] == "REASON" && first_reason) {
                result.reason = f.size() > 1 ? f[1] : "";
                first_reason = false;
            } else if (f[0] == "ERROR" && error.empty()) {
                error = f.size() > 1 ? f[1] : "неизвестная ошибка воркера";
            }
        }
        if (c.report.empty() && error.empty()) error = "воркер завершился без отчёта";
    }
    result.stats.crashes_found = static_cast<long>(result.crashes.size());
    result.stats.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    result.stats.exec_per_sec = result.stats.elapsed > 0 ? static_cast<double>(result.stats.runs) / result.stats.elapsed : 0.0;
    if (!error.empty()) throw std::runtime_error("воркер: " + error);
    return result;
}

}  // namespace wyrm

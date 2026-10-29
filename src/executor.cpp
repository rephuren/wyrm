#include "wyrm/executor.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "wyrm/persistent.hpp"

namespace wyrm {

namespace {
constexpr size_t kMaxCrashOutput = 8192;

std::string read_all(int fd, size_t limit) {
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<size_t>(n));
        if (out.size() >= limit) break;
    }
    return out;
}
}  // namespace

Executor::Executor(const Options& opts, CoverageMap& coverage) : opts_(opts), coverage_(coverage) {
    shm_env_ = "WYRM_SHM_ID=" + coverage_.shm_name();
    if (opts_.persistent || opts_.forkserver) signal(SIGPIPE, SIG_IGN);  // запись в канал упавшей цели не должна убивать фаззер
}

Executor::~Executor() {
    stop_child(true);
    if (input_map_) munmap(input_map_, kInputShmSize);
    if (input_fd_ >= 0) close(input_fd_);
    if (!input_name_.empty()) unlink(input_name_.c_str());
    if (err_fd_ >= 0) close(err_fd_);
}

RunResult Executor::run(const Bytes& input) {
    if (opts_.forkserver && input.size() <= kMaxPersistentInput) return run_forkserver(input);
    if (opts_.persistent && input.size() <= kMaxPersistentInput) return run_persistent(input);
    return run_once(input);
}

void Executor::classify(int status, RunResult& result) {
    if (WIFSIGNALED(status)) {
        result.status = RunResult::kCrash;
        result.signal = WTERMSIG(status);
        return;
    }
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.exit_code = code;
    if (code == 127) throw std::runtime_error("цель не запустилась (exec вернул 127): проверьте путь --target");
    // ASAN_OPTIONS=exitcode=134: явный код краша, который не спутать с обычным ненулевым выходом цели.
    result.status = (code == 134) ? RunResult::kCrash : RunResult::kOk;
}

RunResult Executor::run_once(const Bytes& input) {
    coverage_.clear();
    RunResult result;

    std::string tmp_input_path;
    if (!opts_.use_stdin) {
        tmp_input_path = "/tmp/wyrm_in_XXXXXX";
        std::vector<char> tmpl(tmp_input_path.begin(), tmp_input_path.end());
        tmpl.push_back('\0');
        int fd = mkstemp(tmpl.data());
        if (fd < 0) throw std::runtime_error("mkstemp для входного файла не удался");
        size_t written = 0;
        while (written < input.size()) {
            ssize_t n = write(fd, input.data() + written, input.size() - written);
            if (n <= 0) break;
            written += static_cast<size_t>(n);
        }
        close(fd);
        tmp_input_path.assign(tmpl.data());
    }

    int stdin_pipe[2] = {-1, -1};
    int stderr_pipe[2];
    if (opts_.use_stdin && pipe(stdin_pipe) != 0) throw std::runtime_error("pipe() не удался");
    if (pipe(stderr_pipe) != 0) throw std::runtime_error("pipe() не удался");

    auto start = std::chrono::steady_clock::now();
    pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("fork() не удался");

    if (pid == 0) {
        // дочерний процесс: цель
        if (opts_.use_stdin) {
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        } else {
            close(STDIN_FILENO);
        }
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDOUT_FILENO);

        putenv(const_cast<char*>(shm_env_.c_str()));
        // Детерминированный ASan: один процесс на вход, повторный краш не нужен, а без этого
        // abort_on_error иногда роняет процесс по-другому при повторном запуске.
        setenv("ASAN_OPTIONS", "abort_on_error=1:exitcode=134:detect_leaks=0", 1);
        setenv("UBSAN_OPTIONS", "abort_on_error=1:exitcode=134", 1);

        if (opts_.use_stdin) {
            execl(opts_.target_path.c_str(), opts_.target_path.c_str(), nullptr);
        } else {
            execl(opts_.target_path.c_str(), opts_.target_path.c_str(), tmp_input_path.c_str(), nullptr);
        }
        _exit(127);  // execl не вернулся бы при успехе
    }

    // родитель
    if (opts_.use_stdin) {
        close(stdin_pipe[0]);
        // Пишем без блокировки навсегда: если цель не читает стандартный ввод, просто закрываем.
        fcntl(stdin_pipe[1], F_SETFL, O_NONBLOCK);
        size_t off = 0;
        while (off < input.size()) {
            ssize_t n = write(stdin_pipe[1], input.data() + off, input.size() - off);
            if (n <= 0) break;
            off += static_cast<size_t>(n);
        }
        close(stdin_pipe[1]);
    }
    close(stderr_pipe[1]);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    bool timed_out = false;
    int status = 0;
    const auto deadline = start + std::chrono::duration<double>(opts_.timeout_seconds);
    for (;;) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (std::chrono::steady_clock::now() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            timed_out = true;
            break;
        }
        usleep(500);
    }

    result.crash_output = read_all(stderr_pipe[0], kMaxCrashOutput);
    close(stderr_pipe[0]);
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (!tmp_input_path.empty()) unlink(tmp_input_path.c_str());

    if (timed_out) {
        result.status = RunResult::kTimeout;
    } else {
        classify(status, result);
    }
    result.new_edges = coverage_.merge_new_edges();
    return result;
}

void Executor::ensure_persistent_resources() {
    if (input_map_) return;
    char tmpl[] = "/tmp/wyrm_in_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) throw std::runtime_error("mkstemp для общей памяти входа не удался");
    if (ftruncate(fd, static_cast<off_t>(kInputShmSize)) != 0) {
        close(fd);
        unlink(tmpl);
        throw std::runtime_error("ftruncate для общей памяти входа не удался");
    }
    void* addr = mmap(nullptr, kInputShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        unlink(tmpl);
        throw std::runtime_error("mmap для общей памяти входа не удался");
    }
    input_fd_ = fd;
    input_map_ = static_cast<uint8_t*>(addr);
    input_name_ = tmpl;

    // stderr цели пишем во временный файл, а не в канал: канал переполнился бы между крашами и завис.
    char err_tmpl[] = "/tmp/wyrm_err_XXXXXX";
    int efd = mkstemp(err_tmpl);
    if (efd < 0) throw std::runtime_error("mkstemp для stderr цели не удался");
    unlink(err_tmpl);
    int flags = fcntl(efd, F_GETFL);
    fcntl(efd, F_SETFL, flags | O_APPEND);  // цель дописывает в конец, фаззер обнуляет файл между запусками
    err_fd_ = efd;
    for (int f : {input_fd_, err_fd_}) fcntl(f, F_SETFD, FD_CLOEXEC);
}

void Executor::start_child() {
    int ctl[2], st[2];
    if (pipe(ctl) != 0 || pipe(st) != 0) throw std::runtime_error("pipe() не удался");
    pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("fork() не удался");
    if (pid == 0) {
        dup2(ctl[0], kCtlFd);
        dup2(st[1], kStatusFd);
        close(ctl[0]);
        close(ctl[1]);
        close(st[0]);
        close(st[1]);
        dup2(err_fd_, STDERR_FILENO);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
        }
        std::string input_env = std::string(kInputShmEnv) + "=" + input_name_;
        std::string persistent_env = std::string(opts_.forkserver ? kForkserverEnv : kPersistentEnv) + "=1";
        putenv(const_cast<char*>(shm_env_.c_str()));
        putenv(const_cast<char*>(input_env.c_str()));
        putenv(const_cast<char*>(persistent_env.c_str()));
        setenv("ASAN_OPTIONS", "abort_on_error=1:exitcode=134:detect_leaks=0", 1);
        setenv("UBSAN_OPTIONS", "abort_on_error=1:exitcode=134", 1);
        execl(opts_.target_path.c_str(), opts_.target_path.c_str(), nullptr);
        _exit(127);
    }
    close(ctl[0]);
    close(st[1]);
    fcntl(ctl[1], F_SETFD, FD_CLOEXEC);
    fcntl(st[0], F_SETFD, FD_CLOEXEC);
    pid_ = pid;
    ctl_w_ = ctl[1];
    status_r_ = st[0];
    iterations_ = 0;
    ++restarts_;
}

void Executor::stop_child(bool kill_now) {
    if (ctl_w_ >= 0) close(ctl_w_);  // EOF в канале команд: цель выходит сама
    ctl_w_ = -1;
    if (pid_ > 0) {
        if (kill_now) kill(pid_, SIGKILL);
        int status;
        waitpid(pid_, &status, 0);
    }
    if (status_r_ >= 0) close(status_r_);
    status_r_ = -1;
    pid_ = -1;
}

RunResult Executor::run_persistent(const Bytes& input) {
    ensure_persistent_resources();
    coverage_.clear();
    RunResult result;
    if (pid_ < 0) start_child();

    uint32_t len = static_cast<uint32_t>(input.size());
    std::memcpy(input_map_, &len, sizeof(len));
    if (!input.empty()) std::memcpy(input_map_ + sizeof(len), input.data(), input.size());
    if (ftruncate(err_fd_, 0) != 0) { /* файл ошибок необязателен: хуже будет только текст отчёта */ }

    auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::duration<double>(opts_.timeout_seconds);

    uint32_t cmd = 1;
    bool alive = write(ctl_w_, &cmd, sizeof(cmd)) == static_cast<ssize_t>(sizeof(cmd));
    bool timed_out = false, finished = false;
    int status = 0;

    while (alive && !finished) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            timed_out = true;
            break;
        }
        pollfd pfd{status_r_, POLLIN, 0};
        int rc = poll(&pfd, 1, static_cast<int>(left));
        if (rc < 0) continue;  // прерван сигналом
        if (rc == 0) {
            timed_out = true;
            break;
        }
        uint32_t done = 0;
        ssize_t n = read(status_r_, &done, sizeof(done));
        if (n == static_cast<ssize_t>(sizeof(done))) {
            finished = true;
        } else {
            alive = false;  // EOF: процесс цели завершился посреди запуска
        }
    }

    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (timed_out) {
        stop_child(true);
        result.status = RunResult::kTimeout;
    } else if (!finished) {
        // Цель умерла (или не приняла команду): забираем причину, процесс заменим на следующем входе.
        if (ctl_w_ >= 0) close(ctl_w_);
        ctl_w_ = -1;
        waitpid(pid_, &status, 0);
        pid_ = -1;
        close(status_r_);
        status_r_ = -1;
        classify(status, result);
        if (result.status == RunResult::kCrash) {
            std::string out(kMaxCrashOutput, '\0');
            ssize_t n = pread(err_fd_, out.data(), out.size(), 0);
            out.resize(n > 0 ? static_cast<size_t>(n) : 0);
            result.crash_output = std::move(out);
        }
    } else if (++iterations_ >= opts_.persistent_iterations) {
        stop_child(false);  // плановая замена процесса: копящаяся утечка или порча состояния не должны накапливаться
    }
    result.new_edges = coverage_.merge_new_edges();
    return result;
}

namespace {

enum class Wait { kOk, kTimeout, kEof };

// Читает 4 байта из канала до момента deadline.
Wait read_word(int fd, std::chrono::steady_clock::time_point deadline, uint32_t& out) {
    for (;;) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) return Wait::kTimeout;
        pollfd pfd{fd, POLLIN, 0};
        int rc = poll(&pfd, 1, static_cast<int>(left));
        if (rc < 0) continue;  // прерван сигналом
        if (rc == 0) return Wait::kTimeout;
        return read(fd, &out, sizeof(out)) == static_cast<ssize_t>(sizeof(out)) ? Wait::kOk : Wait::kEof;
    }
}

}  // namespace

// Ждёт приветствие нового сервера. Если цель не запустилась или упала до приветствия, это ошибка запуска, а не вердикт по входу.
bool Executor::await_hello() {
    uint32_t hello = 0;
    Wait w = read_word(status_r_, std::chrono::steady_clock::now() + std::chrono::seconds(10), hello);
    if (w == Wait::kOk) return true;
    stop_child(true);
    return false;
}

RunResult Executor::run_forkserver(const Bytes& input) {
    ensure_persistent_resources();
    coverage_.clear();
    RunResult result;

    uint32_t len = static_cast<uint32_t>(input.size());
    std::memcpy(input_map_, &len, sizeof(len));
    if (!input.empty()) std::memcpy(input_map_ + sizeof(len), input.data(), input.size());

    auto start = std::chrono::steady_clock::now();
    uint32_t child_pid = 0;
    for (int attempt = 0;; ++attempt) {
        if (pid_ < 0) {
            start_child();
            if (!await_hello()) throw std::runtime_error("fork-сервер цели не запустился: проверьте путь --target и что цель собрана с runtime wyrm");
        }
        if (ftruncate(err_fd_, 0) != 0) { /* файл ошибок необязателен */ }
        uint32_t cmd = 1;
        if (write(ctl_w_, &cmd, sizeof(cmd)) == static_cast<ssize_t>(sizeof(cmd)) &&
            read_word(status_r_, start + std::chrono::seconds(5), child_pid) == Wait::kOk) {
            break;
        }
        stop_child(true);  // сервер умер между входами: один раз пробуем с новым
        if (attempt >= 1) throw std::runtime_error("fork-сервер цели не отвечает");
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(opts_.timeout_seconds));
    uint32_t word = 0;
    Wait w = read_word(status_r_, deadline, word);
    bool timed_out = false;
    if (w == Wait::kTimeout) {
        timed_out = true;
        kill(static_cast<pid_t>(child_pid), SIGKILL);  // убиваем только потомка, сервер ждёт его и отчитывается
        w = read_word(status_r_, std::chrono::steady_clock::now() + std::chrono::seconds(2), word);
    }
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (w != Wait::kOk) {
        stop_child(true);  // сервер пропал посреди входа: заменим на следующем
        if (timed_out) {
            result.status = RunResult::kTimeout;
        } else {
            result.status = RunResult::kCrash;
            result.exit_code = -1;
        }
    } else if (timed_out) {
        result.status = RunResult::kTimeout;
    } else {
        classify(static_cast<int>(word), result);
    }
    if (result.status == RunResult::kCrash) {
        std::string out(kMaxCrashOutput, '\0');
        ssize_t n = pread(err_fd_, out.data(), out.size(), 0);
        out.resize(n > 0 ? static_cast<size_t>(n) : 0);
        result.crash_output = std::move(out);
    }
    result.new_edges = coverage_.merge_new_edges();
    return result;
}

}  // namespace wyrm

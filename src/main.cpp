#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "wyrm/fuzzer.hpp"
#include "wyrm/minimizer.hpp"

using namespace wyrm;

namespace {

volatile sig_atomic_t g_stop = 0;
void on_sigint(int) { g_stop = 1; }

const char* kRed = "\033[38;2;179;0;15m";
const char* kDim = "\033[2m";
const char* kReset = "\033[0m";

void print_usage() {
    std::fprintf(stderr,
        "wyrm — coverage-guided фаззер\n\n"
        "использование:\n"
        "  wyrm fuzz --target <путь> --corpus <папка> [опции]\n"
        "  wyrm minimize --target <путь> --input <файл> --output <файл>\n"
        "  wyrm replay --target <путь> --input <файл>\n\n"
        "общие опции:\n"
        "  --corpus <папка>        где хранить/читать входы с покрытием (по умолчанию corpus)\n"
        "  --crashes <папка>       куда писать находки (по умолчанию crashes)\n"
        "  --seed-dir <папка>      начальные сиды для первого запуска\n"
        "  --max-runs <N>          остановиться после N запусков\n"
        "  --max-seconds <N>       остановиться через N секунд\n"
        "  --timeout <N>           таймаут одного запуска в секундах (по умолчанию 2)\n"
        "  --file-input            передавать вход через argv[1], а не stdin\n"
        "  --seed <N>              сид генератора случайных чисел, для воспроизводимости\n");
}

bool has_flag(int argc, char** argv, const char* name) {
    for (int i = 0; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

std::string get_opt(int argc, char** argv, const char* name, const std::string& def = "") {
    for (int i = 0; i < argc - 1; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return def;
}

Bytes read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("не удалось прочитать " + path);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

Options parse_common(int argc, char** argv) {
    Options opts;
    opts.target_path = get_opt(argc, argv, "--target");
    opts.corpus_dir = get_opt(argc, argv, "--corpus", "corpus");
    opts.crashes_dir = get_opt(argc, argv, "--crashes", "crashes");
    opts.use_stdin = !has_flag(argc, argv, "--file-input");
    std::string max_runs = get_opt(argc, argv, "--max-runs");
    if (!max_runs.empty()) opts.max_runs = std::stol(max_runs);
    std::string max_seconds = get_opt(argc, argv, "--max-seconds");
    if (!max_seconds.empty()) opts.max_seconds = std::stod(max_seconds);
    std::string timeout = get_opt(argc, argv, "--timeout");
    if (!timeout.empty()) opts.timeout_seconds = std::stod(timeout);
    std::string seed = get_opt(argc, argv, "--seed");
    if (!seed.empty()) opts.seed = std::stoull(seed);
    if (opts.target_path.empty()) throw std::runtime_error("нужен --target <путь>");
    return opts;
}

int cmd_fuzz(int argc, char** argv) {
    Options opts = parse_common(argc, argv);
    Fuzzer fuzzer(opts);
    std::string seed_dir = get_opt(argc, argv, "--seed-dir");
    if (!seed_dir.empty()) fuzzer.seed_from_dir(seed_dir);
    fuzzer.seed_from_dir(opts.corpus_dir);  // продолжить с места, если корпус уже существует

    std::signal(SIGINT, on_sigint);
    auto last_print = std::chrono::steady_clock::now();
    std::string stop_reason = fuzzer.run(
        [&](const Stats& s) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - last_print).count() < 0.2) return;
            last_print = now;
            std::fprintf(stderr, "\r%sзапусков: %-8ld  покрытие: %-6zu  корпус: %-6zu  крашей: %-4ld  %.0f/с%s   ",
                        kDim, s.runs, s.edges_covered, s.corpus_size, s.crashes_found, s.exec_per_sec, kReset);
        },
        [] { return g_stop != 0; });

    const Stats& s = fuzzer.stats();
    std::fprintf(stderr, "\n\n%s▓ итог (%s)%s\n", kRed, stop_reason.empty() ? "остановлено" : stop_reason.c_str(), kReset);
    std::printf("запусков: %ld   покрытие: %zu рёбер   корпус: %zu входов   крашей: %ld   скорость: %.0f/с\n",
               s.runs, s.edges_covered, s.corpus_size, s.crashes_found, s.exec_per_sec);
    for (const auto& c : fuzzer.crashes())
        std::printf("  %s▓%s %s — %s\n", kRed, kReset, c.file.c_str(), c.reason.c_str());
    return fuzzer.crashes().empty() ? 0 : 1;
}

int cmd_minimize(int argc, char** argv) {
    Options opts = parse_common(argc, argv);
    std::string input_path = get_opt(argc, argv, "--input");
    std::string output_path = get_opt(argc, argv, "--output", input_path + ".min");
    if (input_path.empty()) throw std::runtime_error("нужен --input <файл>");
    Bytes input = read_file(input_path);

    CoverageMap coverage;
    Executor executor(opts, coverage);
    RunResult baseline = executor.run(input);
    if (baseline.status != RunResult::kCrash) {
        std::fprintf(stderr, "исходный вход не падает на этой цели, нечего минимизировать\n");
        return 1;
    }
    Bytes minimized = minimize(input, [&](const Bytes& b) { return executor.run(b).status == RunResult::kCrash; });
    std::ofstream out(output_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(minimized.data()), static_cast<std::streamsize>(minimized.size()));
    std::printf("минимизировано: %zu -> %zu байт, записано в %s\n", input.size(), minimized.size(), output_path.c_str());
    return 0;
}

int cmd_replay(int argc, char** argv) {
    Options opts = parse_common(argc, argv);
    std::string input_path = get_opt(argc, argv, "--input");
    if (input_path.empty()) throw std::runtime_error("нужен --input <файл>");
    Bytes input = read_file(input_path);
    CoverageMap coverage;
    Executor executor(opts, coverage);
    RunResult r = executor.run(input);
    const char* names[] = {"ok", "crash", "timeout", "hang"};
    std::printf("статус: %s  сигнал: %d  код выхода: %d  время: %.3fс\n", names[r.status], r.signal, r.exit_code,
               r.seconds);
    if (!r.crash_output.empty()) std::printf("---\n%s\n", r.crash_output.c_str());
    return r.status == RunResult::kCrash ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }
    std::string mode = argv[1];
    try {
        if (mode == "fuzz") return cmd_fuzz(argc - 2, argv + 2);
        if (mode == "minimize") return cmd_minimize(argc - 2, argv + 2);
        if (mode == "replay") return cmd_replay(argc - 2, argv + 2);
        print_usage();
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ошибка: %s\n", e.what());
        return 2;
    }
}

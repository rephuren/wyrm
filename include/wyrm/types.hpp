#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wyrm {

using Bytes = std::vector<uint8_t>;

struct RunResult {
    enum Status { kOk, kCrash, kTimeout, kHang } status = kOk;
    int exit_code = 0;
    int signal = 0;
    std::string crash_output;  // stderr цели (ASan/UBSan отчёт), обрезан до разумного размера
    double seconds = 0.0;
    size_t new_edges = 0;      // сколько новых рёбер покрытия дал этот вход
};

struct Options {
    std::string target_path;
    std::string corpus_dir;
    std::string crashes_dir = "crashes";
    std::string resume_dir;        // продолжить с существующим состоянием (та же папка, что corpus_dir)
    long max_runs = -1;             // -1 = без ограничения
    double max_seconds = -1.0;      // -1 = без ограничения
    double timeout_seconds = 2.0;
    size_t max_input_size = 1 << 16;
    unsigned jobs = 1;
    uint64_t seed = 0;              // 0 = из времени/энтропии
    bool minimize_on_crash = true;
    bool use_stdin = true;          // true: вход через stdin, false: через argv[1] (путь к файлу)
    bool persistent = false;        // один долгоживущий процесс цели вместо fork+exec на каждый вход
    unsigned persistent_iterations = 2000;  // после стольких входов процесс цели заменяется новым
    std::string worker_tag;         // пусто в одиночном режиме; "w1-" и т. п. у воркера параллельного фаззинга
    double sync_interval = 1.0;     // как часто воркер подтягивает чужие входы из общего корпуса, секунды
};

}  // namespace wyrm

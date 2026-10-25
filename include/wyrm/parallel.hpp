#pragma once
#include <functional>
#include <string>
#include <vector>

#include "wyrm/fuzzer.hpp"

namespace wyrm {

struct ParallelResult {
    Stats stats;                      // суммарные запуски и краши; покрытие и корпус по лучшему воркеру
    std::vector<CrashRecord> crashes;
    std::string reason;               // "max_runs", "max_seconds" или "" при остановке пользователем
    unsigned workers = 0;
};

// Параллельный фаззинг: N процессов-воркеров с общей папкой корпуса. Каждый воркер ведёт свою карту покрытия
// и раз в sync_interval подбирает чужие находки из папки, как режим -M/-S у AFL. Процессы, а не потоки:
// fork+exec из многопоточной программы небезопасен, а так воркеры изолированы друг от друга.
ParallelResult run_parallel(const Options& opts, const std::string& seed_dir, const std::function<void(const Stats&)>& progress,
                            const std::function<bool()>& should_stop);

}  // namespace wyrm

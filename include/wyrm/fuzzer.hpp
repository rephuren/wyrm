#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <unordered_map>

#include "wyrm/corpus.hpp"
#include "wyrm/coverage.hpp"
#include "wyrm/executor.hpp"
#include "wyrm/mutator.hpp"
#include "wyrm/types.hpp"

namespace wyrm {

struct Stats {
    long runs = 0;
    long crashes_found = 0;   // уникальные краши (у каждого сохранён файл)
    long crashes_total = 0;   // все падения, включая повторы того же бага
    size_t corpus_size = 0;
    size_t edges_covered = 0;
    double exec_per_sec = 0.0;
    double elapsed = 0.0;
};

struct CrashRecord {
    std::string file;
    std::string reason;    // первая содержательная строка ASan/UBSan-отчёта
    RunResult::Status status = RunResult::kCrash;
    int signal = 0;
    bool reproducible = true;  // в persistent-режиме краш перепроверяется в чистом процессе
    std::string signature;     // нормализованная причина и верхний кадр: по ней повторы одного бага схлопываются
    long duplicates = 0;       // сколько раз этот же баг нашли повторно
};

// Сигнатура краша: место ошибки без адресов и конкретных индексов, чтобы один баг не плодил сотни записей.
std::string crash_signature(const std::string& reason, const std::string& crash_output, int signal);

// Один рабочий цикл фаззера: выбрать вход, смутировать, запустить, записать находки.
// progress вызывается после каждого прогона (для живого вывода прогресса в CLI); может быть пустым.
class Fuzzer {
public:
    explicit Fuzzer(Options opts);

    void seed_from_dir(const std::string& dir);
    void add_seed(Bytes data);
    // Подхватить всё, что уже лежит в папке корпуса (прошлый запуск или другие воркеры), без перезаписи файлов.
    void resume();
    const Stats& stats() const { return stats_; }
    const std::vector<CrashRecord>& crashes() const { return crashes_; }

    // Возвращает причину остановки: "max_runs", "max_seconds" или "" если прервано снаружи (should_stop).
    std::string run(const std::function<void(const Stats&)>& progress, const std::function<bool()>& should_stop);

private:
    Options opts_;
    CoverageMap coverage_;
    Executor executor_;
    Mutator mutator_;
    Corpus corpus_;
    Stats stats_;
    std::vector<CrashRecord> crashes_;
    std::unordered_map<std::string, size_t> crash_index_;  // сигнатура -> индекс в crashes_
    std::chrono::steady_clock::time_point start_;
    // Проверка крашей, найденных в persistent-режиме: повтор в отдельном чистом процессе.
    Options verify_opts_;
    CoverageMap verify_coverage_;
    std::unique_ptr<Executor> verifier_;

    void handle_result(const Bytes& input, const RunResult& r);
    std::string extract_reason(const std::string& stderr_text) const;
};

}  // namespace wyrm

#pragma once
#include <sys/types.h>

#include <string>

#include "wyrm/coverage.hpp"
#include "wyrm/types.hpp"

namespace wyrm {

// Запускает цель на входе и собирает покрытие. Два режима (Options::persistent):
//  - по умолчанию: отдельный процесс на каждый вход (fork+exec): просто и надёжно, но медленно;
//  - persistent: один долгоживущий процесс, вход приходит через общую память, команды по каналу.
//    Крашится или зависает цель, её процесс заменяется, а вердикт возвращается как обычно.
class Executor {
public:
    Executor(const Options& opts, CoverageMap& coverage);
    ~Executor();
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    RunResult run(const Bytes& input);

    // Сколько раз пришлось запускать процесс цели заново (для persistent: после краша, таймаута или по лимиту итераций).
    long restarts() const { return restarts_; }

private:
    const Options& opts_;
    CoverageMap& coverage_;
    std::string shm_env_;

    // состояние persistent-режима
    pid_t pid_ = -1;
    int ctl_w_ = -1;
    int status_r_ = -1;
    int err_fd_ = -1;
    int input_fd_ = -1;
    uint8_t* input_map_ = nullptr;
    std::string input_name_;
    unsigned iterations_ = 0;
    long restarts_ = 0;

    RunResult run_once(const Bytes& input);
    RunResult run_persistent(const Bytes& input);
    void ensure_persistent_resources();
    void start_child();
    void stop_child(bool kill_now);
    static void classify(int status, RunResult& result);
};

}  // namespace wyrm

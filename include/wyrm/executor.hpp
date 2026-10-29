#pragma once
#include <sys/types.h>

#include <string>

#include "wyrm/coverage.hpp"
#include "wyrm/types.hpp"

namespace wyrm {

// Запускает цель на входе и собирает покрытие. Три режима:
//  - по умолчанию: отдельный процесс на каждый вход (fork+exec): просто и надёжно, но медленно;
//  - persistent: один долгоживущий процесс, вход приходит через общую память, команды по каналу.
//    Крашится или зависает цель, её процесс заменяется, а вердикт возвращается как обычно;
//  - fork-сервер (Options::forkserver): один прогретый процесс-сервер, на каждый вход он делает fork(). Падение или
//    зависание касается только потомка, сервер живёт дальше, а состояние между входами не накапливается.
class Executor {
public:
    Executor(const Options& opts, CoverageMap& coverage);
    ~Executor();
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    RunResult run(const Bytes& input);

    // Сколько раз пришлось запускать процесс цели заново (для persistent: после краша, таймаута или по лимиту итераций;
    // для fork-сервера: сколько раз стартовал сам сервер).
    long restarts() const { return restarts_; }

    // pid живого процесса цели (persistent-процесс или fork-сервер), либо -1. Нужен тестам, чтобы убить его снаружи.
    pid_t child_pid() const { return pid_; }

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
    RunResult run_forkserver(const Bytes& input);
    bool await_hello();
    void ensure_persistent_resources();
    void start_child();
    void stop_child(bool kill_now);
    static void classify(int status, RunResult& result);
};

}  // namespace wyrm

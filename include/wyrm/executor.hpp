#pragma once
#include <string>

#include "wyrm/coverage.hpp"
#include "wyrm/types.hpp"

namespace wyrm {

// Запускает целевой бинарь как отдельный процесс на каждый вход: проще и надёжнее persistent-режима
// для учебного фаззера, хотя и медленнее. Вход передаётся через stdin или временный файл (Options::use_stdin).
class Executor {
public:
    Executor(const Options& opts, CoverageMap& coverage);

    RunResult run(const Bytes& input);

private:
    const Options& opts_;
    CoverageMap& coverage_;
    std::string shm_env_;
};

}  // namespace wyrm

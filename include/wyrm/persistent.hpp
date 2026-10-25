#pragma once
#include <cstddef>
#include <cstdint>

namespace wyrm {

// Протокол persistent-режима между фаззером и целью. Общие константы: их видят и фаззер, и среда выполнения цели.
constexpr const char* kPersistentEnv = "WYRM_PERSISTENT";   // переменная окружения: цель работает в цикле
constexpr const char* kInputShmEnv = "WYRM_INPUT_SHM";      // имя файла с общей памятью входа
constexpr int kCtlFd = 198;                                 // фаззер -> цель: «выполни вход» (4 байта), EOF = выйти
constexpr int kStatusFd = 199;                              // цель -> фаззер: «готово» (4 байта)
constexpr size_t kMaxPersistentInput = 1 << 20;             // вход крупнее выполняется обычным запуском
constexpr size_t kInputShmSize = sizeof(uint32_t) + kMaxPersistentInput;  // [длина:4][данные]

}  // namespace wyrm

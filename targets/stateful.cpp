// Учебная цель-ловушка для persistent-режима: падает не из-за входа, а из-за накопленного состояния.
// Седьмой вызов в одном процессе роняет цель (запись по нулевому указателю), хотя в чистом процессе тот же
// вход безопасен. Так проверяется, что wyrm не выдаёт такие падения за воспроизводимые баги.
// Вход, начинающийся с 'H', зависает: на нём проверяется обработка таймаута.
#include <cstddef>
#include <cstdint>

namespace {
int calls = 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 0 && data[0] == 'H') {
        volatile bool spin = true;
        while (spin) {
        }
    }
    if (++calls == 7) *static_cast<volatile int*>(nullptr) = 1;
    return 0;
}

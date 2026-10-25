// Учебная цель: три намеренных бага за байтом-переключателем входа, чтобы показать,
// что coverage-guided фаззер находит их все, а не только первый на пути выполнения.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

void bug_overflow(const std::vector<uint8_t>& d) {
    char buf[16];
    // БАГ: копирует весь остаток входа без проверки длины против размера buf.
    size_t n = d.size() - 1;
    for (size_t i = 0; i < n; ++i) buf[i] = static_cast<char>(d[i + 1]);
    if (buf[0] == 'Z') std::cerr << "z\n";  // не даёт оптимизатору выкинуть запись
}

void bug_div_zero(const std::vector<uint8_t>& d) {
    if (d.size() < 3) return;
    int a = d[1], b = d[2] - 42;  // b == 0, когда d[2] == 42
    std::cerr << (a / b) << "\n";
}

void bug_oob_read(const std::vector<uint8_t>& d) {
    if (d.size() < 6) return;
    uint32_t idx;
    std::memcpy(&idx, d.data() + 2, 4);
    int table[4] = {1, 2, 3, 4};
    // БАГ: idx приходит из входа и не проверяется — выход за границы массива.
    std::cerr << table[idx % 1000000] << "\n";
}

}  // namespace

// Точка входа в стиле libFuzzer: main() и persistent-цикл даёт среда выполнения wyrm (persistent_rt.cpp).
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* bytes, size_t size) {
    std::vector<uint8_t> d(bytes, bytes + size);
    if (d.empty()) return 0;
    switch (d[0]) {
        case 'O': bug_overflow(d); break;
        case 'D': bug_div_zero(d); break;
        case 'R': bug_oob_read(d); break;
        default: break;
    }
    return 0;
}

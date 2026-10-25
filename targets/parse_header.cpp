// Учебная цель: разбор простого заголовка "MAGIC<len:4 LE><len байт имени>".
// Намеренный баг для демонстрации: длина не проверяется против реального размера буфера входа.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

struct Header {
    uint32_t len;
    char name[64];  // фиксированный буфер — при len > 64 будет переполнение
};

bool parse(const std::vector<uint8_t>& data, Header& out) {
    if (data.size() < 8 || std::memcmp(data.data(), "MAGC", 4) != 0) return false;
    uint32_t len;
    std::memcpy(&len, data.data() + 4, 4);
    out.len = len;
    if (data.size() < 8 + len) return false;
    // БАГ: len не ограничена размером out.name (64 байта) — классическое переполнение буфера.
    std::memcpy(out.name, data.data() + 8, len);
    return true;
}

}  // namespace

int main() {
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    Header h{};
    if (parse(data, h)) std::cerr << "parsed, len=" << h.len << "\n";
    return 0;
}

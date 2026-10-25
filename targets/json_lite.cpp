// Учебная цель посложнее: крошечный разборщик JSON-подобного синтаксиса (объекты, строки, числа).
// Намеренный баг: рекурсивный спуск без ограничения глубины — глубокая вложенность переполняет стек.
#include <cstdint>
#include <iostream>
#include <string>

namespace {

struct Parser {
    const char* p;
    const char* end;
    int depth = 0;

    void skip_ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\t')) ++p; }

    bool parse_value() {
        skip_ws();
        if (p >= end) return false;
        ++depth;  // БАГ: нет проверки depth против лимита — глубокая вложенность переполняет стек
        bool ok;
        if (*p == '{') ok = parse_object();
        else if (*p == '[') ok = parse_array();
        else if (*p == '"') ok = parse_string();
        else if ((*p >= '0' && *p <= '9') || *p == '-') ok = parse_number();
        else if (end - p >= 4 && std::string(p, 4) == "true") { p += 4; ok = true; }
        else if (end - p >= 4 && std::string(p, 4) == "null") { p += 4; ok = true; }
        else ok = false;
        --depth;
        return ok;
    }

    bool parse_object() {
        ++p;
        skip_ws();
        if (p < end && *p == '}') { ++p; return true; }
        for (;;) {
            skip_ws();
            if (p >= end || *p != '"' || !parse_string()) return false;
            skip_ws();
            if (p >= end || *p != ':') return false;
            ++p;
            if (!parse_value()) return false;
            skip_ws();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == '}') { ++p; return true; }
            return false;
        }
    }

    bool parse_array() {
        ++p;
        skip_ws();
        if (p < end && *p == ']') { ++p; return true; }
        for (;;) {
            if (!parse_value()) return false;
            skip_ws();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == ']') { ++p; return true; }
            return false;
        }
    }

    bool parse_string() {
        ++p;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) ++p;
            ++p;
        }
        if (p >= end) return false;
        ++p;
        return true;
    }

    bool parse_number() {
        const char* start = p;
        if (p < end && *p == '-') ++p;
        while (p < end && *p >= '0' && *p <= '9') ++p;
        if (p < end && *p == '.') {
            ++p;
            while (p < end && *p >= '0' && *p <= '9') ++p;
        }
        return p > start;
    }
};

}  // namespace

int main() {
    std::string data((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    Parser parser{data.data(), data.data() + data.size()};
    bool ok = parser.parse_value();
    std::cerr << (ok ? "ok" : "fail") << "\n";
    return 0;
}

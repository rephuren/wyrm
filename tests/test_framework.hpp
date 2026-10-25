#pragma once
// Минимальный тестовый фреймворк без внешних зависимостей: регистрация через статические
// объекты, как в Catch2/doctest, только в 40 строк.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace wyrmtest {

struct Case {
    std::string name;
    std::function<void()> fn;
};
inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}
struct Register {
    Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};
struct Failure {
    std::string message;
};

inline int run_all() {
    int failed = 0;
    for (auto& c : registry()) {
        try {
            c.fn();
            std::printf("  ok    %s\n", c.name.c_str());
        } catch (const Failure& f) {
            std::printf("  FAIL  %s: %s\n", c.name.c_str(), f.message.c_str());
            ++failed;
        } catch (const std::exception& e) {
            std::printf("  ERROR %s: %s\n", c.name.c_str(), e.what());
            ++failed;
        }
    }
    std::printf("%d/%zu passed\n", static_cast<int>(registry().size()) - failed, registry().size());
    return failed;
}

}  // namespace wyrmtest

#define WYRM_CONCAT2(a, b) a##b
#define WYRM_CONCAT(a, b) WYRM_CONCAT2(a, b)
#define TEST(name)                                                                      \
    static void WYRM_CONCAT(test_fn_, __LINE__)();                                      \
    static wyrmtest::Register WYRM_CONCAT(test_reg_, __LINE__)(name, WYRM_CONCAT(test_fn_, __LINE__)); \
    static void WYRM_CONCAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                             \
    do {                                                                                        \
        if (!(cond)) throw wyrmtest::Failure{std::string("CHECK failed: ") + #cond + " at " + __FILE__ + ":" + std::to_string(__LINE__)}; \
    } while (0)

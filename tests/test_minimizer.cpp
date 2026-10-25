#include "test_framework.hpp"
#include "wyrm/minimizer.hpp"

using namespace wyrm;

namespace {
// "Падает", если после первого байта 0xFF встречаются байты 0xAA 0xBB где-нибудь дальше.
bool crashes_if_contains_marker(const Bytes& b) {
    if (b.empty() || b[0] != 0xFF) return false;
    for (size_t i = 1; i + 1 < b.size(); ++i)
        if (b[i] == 0xAA && b[i + 1] == 0xBB) return true;
    return false;
}
}  // namespace

TEST("minimize убирает весь мусор вокруг минимального триггера") {
    Bytes crashing = {0xFF, 1, 2, 3, 4, 5, 0xAA, 0xBB, 6, 7, 8, 9, 10};
    CHECK(crashes_if_contains_marker(crashing));
    Bytes min = minimize(crashing, crashes_if_contains_marker);
    CHECK(crashes_if_contains_marker(min));
    CHECK(min.size() <= crashing.size());
    CHECK(min.size() <= 4);  // минимум: 0xFF, ..., 0xAA, 0xBB — ожидаем компактный результат
}

TEST("minimize не ломает уже минимальный вход") {
    Bytes already_min = {0xFF, 0xAA, 0xBB};
    Bytes min = minimize(already_min, crashes_if_contains_marker);
    CHECK(crashes_if_contains_marker(min));
    CHECK(min.size() <= already_min.size());
}

TEST("minimize работает, когда still_crashes всегда true (вход схлопывается, но не обнуляется до пустого)") {
    Bytes any = {1, 2, 3, 4};
    Bytes min = minimize(any, [](const Bytes&) { return true; });
    CHECK(!min.empty());
}

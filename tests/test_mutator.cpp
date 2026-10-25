#include "test_framework.hpp"
#include "wyrm/mutator.hpp"

using namespace wyrm;

TEST("mutate никогда не превышает max_size") {
    Mutator m(1);
    Bytes input = {1, 2, 3, 4, 5};
    for (int i = 0; i < 200; ++i) {
        Bytes out = m.mutate(input, 8);
        CHECK(out.size() <= 8);
    }
}

TEST("mutate на пустом входе не падает и даёт непустой результат") {
    Mutator m(2);
    Bytes out = m.mutate({}, 32);
    CHECK(!out.empty());
}

TEST("mutate детерминирован при одинаковом сиде") {
    Mutator a(42), b(42);
    Bytes input = {10, 20, 30, 40, 50, 60, 70, 80};
    for (int i = 0; i < 20; ++i) CHECK(a.mutate(input, 64) == b.mutate(input, 64));
}

TEST("разные сиды почти всегда дают разные мутации") {
    Mutator a(1), b(2);
    Bytes input(32, 0x41);
    int same = 0;
    for (int i = 0; i < 50; ++i)
        if (a.mutate(input, 64) == b.mutate(input, 64)) ++same;
    CHECK(same < 10);
}

TEST("splice режет и склеивает фрагменты обоих входов") {
    Mutator m(3);
    Bytes a(20, 0xAA), b(20, 0xBB);
    bool saw_mixed = false;
    for (int i = 0; i < 50; ++i) {
        Bytes s = m.splice(a, b, 64);
        CHECK(s.size() <= 64);
        bool has_a = false, has_b = false;
        for (auto x : s) { has_a |= (x == 0xAA); has_b |= (x == 0xBB); }
        saw_mixed |= (has_a && has_b);
    }
    CHECK(saw_mixed);
}

TEST("splice с пустым входом возвращает другой вход") {
    Mutator m(4);
    Bytes a = {1, 2, 3};
    CHECK(m.splice(a, {}, 64) == a);
    CHECK(m.splice({}, a, 64) == a);
}

TEST("random_bytes уважает границы длины") {
    Mutator m(5);
    for (int i = 0; i < 50; ++i) {
        Bytes b = m.random_bytes(3, 7);
        CHECK(b.size() >= 3 && b.size() <= 7);
    }
}

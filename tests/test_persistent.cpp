// Тесты persistent-режима: один долгоживущий процесс цели вместо fork+exec на каждый вход.
#include <chrono>
#include <filesystem>

#include "test_framework.hpp"
#include "wyrm/fuzzer.hpp"
#include "wyrm/persistent.hpp"

using namespace wyrm;
namespace fs = std::filesystem;

namespace {

fs::path target(const char* name) {
    for (fs::path dir : {fs::current_path(), fs::current_path().parent_path()}) {
        fs::path p = dir / name;
        if (fs::exists(p)) return p;
    }
    throw std::runtime_error(std::string("не найдена собранная цель ") + name);
}

Options persistent_opts(const char* name) {
    Options o;
    o.target_path = target(name).string();
    o.persistent = true;
    o.timeout_seconds = 2.0;
    return o;
}

fs::path temp_dir(const char* name) {
    fs::path p = fs::temp_directory_path() / (std::string("wyrm_persist_") + name);
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

double seconds_for(Executor& exec, const Bytes& input, int runs) {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < runs; ++i) exec.run(input);
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

TEST("persistent: цель живёт между входами, процесс не пересоздаётся на каждый запуск") {
    Options o = persistent_opts("target_json_lite");
    CoverageMap cov;
    Executor exec(o, cov);
    for (int i = 0; i < 50; ++i) CHECK(exec.run({'{', '}'}).status == RunResult::kOk);
    CHECK(exec.restarts() == 1);
}

TEST("persistent: покрытие собирается и сбрасывается между входами") {
    Options o = persistent_opts("target_vuln_buffer");
    CoverageMap cov;
    Executor exec(o, cov);
    RunResult first = exec.run({'x'});
    CHECK(first.new_edges > 0);
    CHECK(exec.run({'x'}).new_edges == 0);          // то же покрытие второй раз не «новое»
    CHECK(exec.run({'O', 1, 2}).new_edges > 0);      // другая ветка даёт новые рёбра
}

TEST("persistent: краш виден как краш, процесс заменяется, следующий вход выполняется нормально") {
    Options o = persistent_opts("target_vuln_buffer");
    CoverageMap cov;
    Executor exec(o, cov);
    CHECK(exec.run({'x'}).status == RunResult::kOk);
    RunResult crash = exec.run({'D', 'x', 42, 'y'});  // деление на ноль (UBSan)
    CHECK(crash.status == RunResult::kCrash);
    CHECK(crash.crash_output.find("runtime error") != std::string::npos);
    CHECK(exec.run({'x'}).status == RunResult::kOk);
    CHECK(exec.restarts() == 2);
    RunResult overflow = exec.run({'O', 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20});
    CHECK(overflow.status == RunResult::kCrash);
    CHECK(overflow.crash_output.find("AddressSanitizer") != std::string::npos || overflow.crash_output.find("runtime error") != std::string::npos);
}

TEST("persistent: вывод ASan от прошлых входов не попадает в отчёт следующего краша") {
    Options o = persistent_opts("target_parse_header");  // печатает в stderr на каждом разобранном заголовке
    CoverageMap cov;
    Executor exec(o, cov);
    Bytes ok = {'M', 'A', 'G', 'C', 1, 0, 0, 0, 'x'};
    for (int i = 0; i < 5; ++i) exec.run(ok);
    Bytes bad = {'M', 'A', 'G', 'C', 200, 0, 0, 0};
    bad.insert(bad.end(), 200, 'A');
    RunResult r = exec.run(bad);
    CHECK(r.status == RunResult::kCrash);
    CHECK(r.crash_output.find("parsed, len=1") == std::string::npos);
}

TEST("persistent: зависание обрабатывается как таймаут, процесс убивается и заменяется") {
    Options o = persistent_opts("target_stateful");
    o.timeout_seconds = 0.5;
    CoverageMap cov;
    Executor exec(o, cov);
    CHECK(exec.run({'a'}).status == RunResult::kOk);
    auto t0 = std::chrono::steady_clock::now();
    CHECK(exec.run({'H'}).status == RunResult::kTimeout);
    double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(took < 3.0);
    CHECK(exec.run({'a'}).status == RunResult::kOk);
    CHECK(exec.restarts() == 2);
}

TEST("persistent: плановая замена процесса после persistent_iterations входов") {
    Options o = persistent_opts("target_json_lite");
    o.persistent_iterations = 3;
    CoverageMap cov;
    Executor exec(o, cov);
    for (int i = 0; i < 10; ++i) CHECK(exec.run({'{', '}'}).status == RunResult::kOk);
    CHECK(exec.restarts() == 4);  // ceil(10 / 3)
}

TEST("persistent: вход больше общей памяти выполняется обычным запуском") {
    Options o = persistent_opts("target_json_lite");
    CoverageMap cov;
    Executor exec(o, cov);
    Bytes big(kMaxPersistentInput + 10, '1');
    CHECK(exec.run(big).status == RunResult::kOk);
    CHECK(exec.restarts() == 0);  // persistent-процесс даже не запускали
}

TEST("persistent: несуществующая цель даёт понятную ошибку, а не бесконечный перезапуск") {
    Options o;
    o.target_path = "/нет/такой/цели";
    o.persistent = true;
    CoverageMap cov;
    Executor exec(o, cov);
    bool threw = false;
    try {
        exec.run({'x'});
    } catch (const std::runtime_error& e) {
        threw = std::string(e.what()).find("--target") != std::string::npos;
    }
    CHECK(threw);
}

TEST("persistent: заметно быстрее fork+exec на каждый вход") {
    Options fast = persistent_opts("target_json_lite");
    Options slow = fast;
    slow.persistent = false;
    CoverageMap c1, c2;
    Executor pe(fast, c1), se(slow, c2);
    Bytes in = {'{', '"', 'a', '"', ':', '1', '}'};
    pe.run(in);  // прогрев: старт процесса не считается
    double persistent = seconds_for(pe, in, 300);
    double forked = seconds_for(se, in, 300);
    CHECK(persistent * 2 < forked);
}

TEST("persistent: фаззер находит баги vuln_buffer и краши воспроизводятся в чистом процессе") {
    auto dir = temp_dir("fuzz");
    Options o = persistent_opts("target_vuln_buffer");
    o.corpus_dir = (dir / "corpus").string();
    o.crashes_dir = (dir / "crashes").string();
    o.max_runs = 15000;
    o.seed = 7;
    Fuzzer fuzzer(o);
    fuzzer.add_seed({'O', 1, 2, 3});
    fuzzer.add_seed({'D', 1, 2, 3});
    fuzzer.add_seed({'R', 1, 2, 3, 4, 5});
    fuzzer.run(nullptr, nullptr);
    CHECK(fuzzer.stats().crashes_total >= 2);
    CHECK(!fuzzer.crashes().empty());
    for (const auto& c : fuzzer.crashes()) CHECK(c.reproducible);
    fs::remove_all(dir);
}

TEST("persistent: краш от накопленного состояния помечается как невоспроизводимый") {
    auto dir = temp_dir("stateful");
    Options o = persistent_opts("target_stateful");
    o.corpus_dir = (dir / "corpus").string();
    o.crashes_dir = (dir / "crashes").string();
    o.max_runs = 20;
    o.seed = 3;
    Fuzzer fuzzer(o);
    fuzzer.add_seed({'a', 'b'});
    fuzzer.run(nullptr, nullptr);
    CHECK(!fuzzer.crashes().empty());
    CHECK(!fuzzer.crashes().front().reproducible);
    CHECK(fuzzer.crashes().front().reason.find("не воспроизводится") != std::string::npos);
    fs::remove_all(dir);
}

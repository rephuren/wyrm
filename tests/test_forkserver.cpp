// Тесты fork-сервера: прогретый процесс цели форкается на каждый вход.
#include <signal.h>

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

Options fork_opts(const char* name) {
    Options o;
    o.target_path = target(name).string();
    o.forkserver = true;
    o.timeout_seconds = 2.0;
    return o;
}

double seconds_for(Executor& exec, const Bytes& input, int runs) {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < runs; ++i) exec.run(input);
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

TEST("forkserver: сервер стартует один раз, а входы выполняются в потомках") {
    Options o = fork_opts("target_json_lite");
    CoverageMap cov;
    Executor exec(o, cov);
    for (int i = 0; i < 100; ++i) CHECK(exec.run({'{', '}'}).status == RunResult::kOk);
    CHECK(exec.restarts() == 1);
}

TEST("forkserver: покрытие собирается и не копится между входами") {
    Options o = fork_opts("target_vuln_buffer");
    CoverageMap cov;
    Executor exec(o, cov);
    CHECK(exec.run({'x'}).new_edges > 0);
    CHECK(exec.run({'x'}).new_edges == 0);
    CHECK(exec.run({'O', 1, 2}).new_edges > 0);
}

TEST("forkserver: краш потомка виден как краш, а сервер живёт и не перезапускается") {
    Options o = fork_opts("target_vuln_buffer");
    CoverageMap cov;
    Executor exec(o, cov);
    CHECK(exec.run({'x'}).status == RunResult::kOk);
    RunResult crash = exec.run({'D', 'x', 42, 'y'});  // деление на ноль (UBSan)
    CHECK(crash.status == RunResult::kCrash);
    CHECK(crash.crash_output.find("runtime error") != std::string::npos);
    CHECK(exec.run({'x'}).status == RunResult::kOk);
    RunResult overflow = exec.run({'O', 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20});
    CHECK(overflow.status == RunResult::kCrash);
    CHECK(exec.run({'x'}).status == RunResult::kOk);
    CHECK(exec.restarts() == 1);  // в отличие от persistent: падение не стоит перезапуска процесса
}

TEST("forkserver: накопленное состояние не копится, цель stateful не падает на седьмом вызове") {
    Options o = fork_opts("target_stateful");
    CoverageMap cov;
    Executor exec(o, cov);
    for (int i = 0; i < 30; ++i) CHECK(exec.run({'a'}).status == RunResult::kOk);
}

TEST("forkserver: зависание убивает только потомка, сервер остаётся") {
    Options o = fork_opts("target_stateful");
    o.timeout_seconds = 0.5;
    CoverageMap cov;
    Executor exec(o, cov);
    CHECK(exec.run({'a'}).status == RunResult::kOk);
    auto t0 = std::chrono::steady_clock::now();
    CHECK(exec.run({'H'}).status == RunResult::kTimeout);
    double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(took < 3.0);
    CHECK(exec.run({'a'}).status == RunResult::kOk);
    CHECK(exec.restarts() == 1);
}

TEST("forkserver: вывод ASan от прошлых входов не попадает в отчёт следующего краша") {
    Options o = fork_opts("target_parse_header");
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

TEST("forkserver: вход больше общей памяти выполняется обычным запуском") {
    Options o = fork_opts("target_json_lite");
    CoverageMap cov;
    Executor exec(o, cov);
    Bytes big(kMaxPersistentInput + 10, '1');
    CHECK(exec.run(big).status == RunResult::kOk);
    CHECK(exec.restarts() == 0);
}

TEST("forkserver: несуществующая цель даёт понятную ошибку") {
    Options o;
    o.target_path = "/нет/такой/цели";
    o.forkserver = true;
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

TEST("forkserver: если сервер убит снаружи, следующий вход поднимает новый") {
    Options o = fork_opts("target_json_lite");
    CoverageMap cov;
    Executor exec(o, cov);
    CHECK(exec.run({'{', '}'}).status == RunResult::kOk);
    CHECK(exec.child_pid() > 0);
    kill(exec.child_pid(), SIGKILL);
    CHECK(exec.run({'{', '}'}).status == RunResult::kOk);
    CHECK(exec.restarts() == 2);
}

TEST("forkserver: быстрее fork+exec на каждый вход") {
    Options fs_o = fork_opts("target_json_lite");
    Options slow = fs_o;
    slow.forkserver = false;
    CoverageMap c1, c2;
    Executor fe(fs_o, c1), se(slow, c2);
    Bytes in = {'{', '"', 'a', '"', ':', '1', '}'};
    fe.run(in);  // прогрев: старт сервера не считается
    double forked = seconds_for(fe, in, 300);
    double execd = seconds_for(se, in, 300);
    CHECK(forked < execd);
}

TEST("forkserver: фаззер находит баги vuln_buffer") {
    fs::path dir = fs::temp_directory_path() / "wyrm_forkserver_fuzz";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Options o = fork_opts("target_vuln_buffer");
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
    for (const auto& c : fuzzer.crashes()) CHECK(c.reproducible);
    fs::remove_all(dir);
}

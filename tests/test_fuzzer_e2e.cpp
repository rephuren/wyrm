// Сквозные тесты: гоняют настоящие скомпилированные цели targets/*.cpp как подпроцессы
// и проверяют, что фаззер находит их намеренные баги за разумное число запусков.
#include <filesystem>

#include "test_framework.hpp"
#include "wyrm/fuzzer.hpp"
#include "wyrm/minimizer.hpp"

using namespace wyrm;
namespace fs = std::filesystem;

namespace {

fs::path find_target(const char* name) {
    // CTest запускает бинарь тестов из своей build-директории; цели собраны туда же.
    for (fs::path dir : {fs::current_path(), fs::current_path().parent_path()}) {
        fs::path p = dir / name;
        if (fs::exists(p)) return p;
    }
    throw std::runtime_error(std::string("не найдена собранная цель ") + name + " рядом с тестовым бинарём");
}

fs::path temp_dir(const char* name) {
    fs::path p = fs::temp_directory_path() / (std::string("wyrm_e2e_") + name);
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

}  // namespace

namespace {
// Затравка с полем длины=10 (безопасно) и щедрым запасом байт после неё (140), чтобы проверка
// `data.size() >= 8+len` в цели была удовлетворена для ЛЮБОГО значения len, которое мутатор
// впишет в эти 4 байта. Это разрывает связь «длина согласована с размером», которую слепой
// мутатор иначе почти не может нащупать случайно (классическая трудность для havoc-фаззинга
// без знания формата) — ищем только одно число, а не два согласованных.
Bytes parse_header_seed() {
    Bytes b = {'M', 'A', 'G', 'C', 10, 0, 0, 0};
    b.insert(b.end(), 140, 'A');
    return b;
}
}  // namespace

TEST("фаззер находит переполнение буфера в parse_header за разумное время") {
    auto dir = temp_dir("parse_header");
    Options opts;
    opts.target_path = find_target("target_parse_header").string();
    opts.corpus_dir = (dir / "corpus").string();
    opts.crashes_dir = (dir / "crashes").string();
    opts.max_runs = 20000;
    opts.timeout_seconds = 1.0;
    opts.seed = 1234;

    Fuzzer fuzzer(opts);
    fuzzer.add_seed({'M', 'A', 'G', 'C', 1, 0, 0, 0, 'x'});  // валидный, len=1 — короткое имя
    fuzzer.add_seed(parse_header_seed());
    fuzzer.run(nullptr, nullptr);

    CHECK(fuzzer.stats().crashes_found > 0);
    CHECK(!fuzzer.crashes().empty());
    CHECK(fuzzer.crashes().front().status == RunResult::kCrash);
    fs::remove_all(dir);
}

TEST("фаззер находит все три бага в vuln_buffer (overflow, div-by-zero, oob-read)") {
    auto dir = temp_dir("vuln_buffer");
    Options opts;
    opts.target_path = find_target("target_vuln_buffer").string();
    opts.corpus_dir = (dir / "corpus").string();
    opts.crashes_dir = (dir / "crashes").string();
    opts.max_runs = 15000;
    opts.timeout_seconds = 1.0;
    opts.seed = 7;

    Fuzzer fuzzer(opts);
    fuzzer.add_seed({'O', 1, 2, 3});
    fuzzer.add_seed({'D', 1, 2, 3});
    fuzzer.add_seed({'R', 1, 2, 3, 4, 5});
    fuzzer.run(nullptr, nullptr);

    bool saw_sig = false, saw_asan = false, saw_ubsan = false;
    for (const auto& c : fuzzer.crashes()) {
        saw_sig |= (c.status == RunResult::kCrash);
        saw_asan |= c.reason.find("AddressSanitizer") != std::string::npos;
        saw_ubsan |= c.reason.find("runtime error") != std::string::npos;
    }
    CHECK(fuzzer.stats().crashes_total >= 2);  // падений минимум два за отведённый бюджет — не флаки
    CHECK(fuzzer.stats().crashes_found >= 1 && fuzzer.stats().crashes_found <= fuzzer.stats().crashes_total);
    CHECK(saw_sig);
    CHECK(saw_asan || saw_ubsan);
    fs::remove_all(dir);
}

TEST("minimize на найденном крашеобразующем входе даёт компактный воспроизводимый PoC") {
    auto dir = temp_dir("minimize_e2e");
    Options opts;
    opts.target_path = find_target("target_vuln_buffer").string();
    opts.corpus_dir = (dir / "corpus").string();
    opts.crashes_dir = (dir / "crashes").string();
    opts.max_runs = 500;
    opts.timeout_seconds = 1.0;
    opts.seed = 99;

    Fuzzer fuzzer(opts);
    fuzzer.add_seed({'D', 1, 2, 3});
    // 'R' почти всегда падает уже на первом запуске (idx из входа редко попадает в safe-диапазон
    // [0,4) после %1000000), так что краш гарантирован без значительного бюджета на мутации.
    fuzzer.add_seed({'R', 1, 2, 3, 4, 5});
    fuzzer.run(nullptr, nullptr);
    CHECK(!fuzzer.crashes().empty());

    Bytes crashing = {'D', 'x', static_cast<uint8_t>(42), 'y', 'z'};  // d[2]-42==0 триггерит деление на ноль
    CoverageMap cov;
    Executor exec(opts, cov);
    CHECK(exec.run(crashing).status == RunResult::kCrash);

    Bytes min = minimize(crashing, [&](const Bytes& b) { return exec.run(b).status == RunResult::kCrash; });
    CHECK(min.size() <= crashing.size());
    CHECK(exec.run(min).status == RunResult::kCrash);
    fs::remove_all(dir);
}

TEST("replay даёт одинаковый вердикт на невинном входе дважды подряд") {
    Options opts;
    opts.target_path = find_target("target_json_lite").string();
    CoverageMap cov;
    Executor exec(opts, cov);
    Bytes benign = {'{', '}'};
    CHECK(exec.run(benign).status == RunResult::kOk);
    CHECK(exec.run(benign).status == RunResult::kOk);
}

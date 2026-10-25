// Дедупликация крашей: один и тот же баг должен давать одну запись, а разные баги разные.
#include <filesystem>

#include "test_framework.hpp"
#include "wyrm/fuzzer.hpp"

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
}  // namespace

TEST("сигнатура краша не зависит от адресов и конкретных индексов") {
    std::string a = crash_signature("t.cpp:31:18: runtime error: index 148994 out of bounds for type 'int[4]'", "", 6);
    std::string b = crash_signature("t.cpp:31:18: runtime error: index 17538 out of bounds for type 'int[4]'", "", 6);
    CHECK(a == b);
    std::string c = crash_signature("ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000050", "    #0 0x1051a in parse t.cpp:12", 6);
    std::string d = crash_signature("ERROR: AddressSanitizer: heap-buffer-overflow on address 0x6020000000a0", "    #0 0x2b3c4 in parse t.cpp:12", 6);
    CHECK(c == d);
}

TEST("сигнатура краша различает разные места ошибки и разные причины") {
    CHECK(crash_signature("t.cpp:15:36: runtime error: index 16 out of bounds", "", 6) != crash_signature("t.cpp:31:18: runtime error: index 16 out of bounds", "", 6));
    CHECK(crash_signature("ERROR: AddressSanitizer: heap-buffer-overflow", "    #0 0x1 in f t.cpp:1", 6) !=
          crash_signature("ERROR: AddressSanitizer: heap-buffer-overflow", "    #0 0x1 in g t.cpp:9", 6));
    CHECK(crash_signature("x", "", 6) != crash_signature("x", "", 11));
}

TEST("повторы одного бага схлопываются в одну запись с счётчиком, файл один") {
    fs::path dir = fs::temp_directory_path() / "wyrm_dedup_run";
    fs::remove_all(dir);
    Options o;
    o.target_path = target("target_vuln_buffer").string();
    o.corpus_dir = (dir / "corpus").string();
    o.crashes_dir = (dir / "crashes").string();
    o.max_runs = 1500;
    o.timeout_seconds = 1.0;
    o.seed = 21;
    o.persistent = true;
    Fuzzer fuzzer(o);
    fuzzer.add_seed({'R', 1, 2, 3, 4, 5});  // выход за границы массива: падает почти на любом индексе
    fuzzer.add_seed({'R', 9, 9, 9, 9, 9});
    fuzzer.run(nullptr, nullptr);
    CHECK(fuzzer.stats().crashes_total > fuzzer.stats().crashes_found);
    CHECK(fuzzer.stats().crashes_found == static_cast<long>(fuzzer.crashes().size()));
    size_t files = 0;
    for (const auto& e : fs::directory_iterator(o.crashes_dir)) files += e.is_regular_file();
    CHECK(files == fuzzer.crashes().size());
    long counted = 0;
    for (const auto& c : fuzzer.crashes()) counted += c.duplicates + 1;
    CHECK(counted == fuzzer.stats().crashes_total);
    fs::remove_all(dir);
}

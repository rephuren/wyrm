#include <filesystem>
#include <random>

#include "test_framework.hpp"
#include "wyrm/corpus.hpp"

using namespace wyrm;
namespace fs = std::filesystem;

namespace {
fs::path temp_dir(const char* name) {
    fs::path p = fs::temp_directory_path() / (std::string("wyrm_test_") + name);
    fs::remove_all(p);
    return p;
}
}  // namespace

TEST("add сохраняет вход на диск и делает его доступным через pick") {
    auto dir = temp_dir("corpus_add");
    Corpus c(dir);
    c.add({1, 2, 3});
    c.add({4, 5, 6});
    CHECK(c.size() == 2);
    std::mt19937_64 rng(1);
    bool saw1 = false, saw2 = false;
    for (int i = 0; i < 30; ++i) {
        const Bytes& b = c.pick(rng);
        saw1 |= (b == Bytes{1, 2, 3});
        saw2 |= (b == Bytes{4, 5, 6});
    }
    CHECK(saw1 && saw2);
    fs::remove_all(dir);
}

TEST("load_existing восстанавливает корпус после перезапуска") {
    auto dir = temp_dir("corpus_resume");
    {
        Corpus c(dir);
        c.add({9, 9, 9});
        c.add({1, 0, 1, 0});
    }
    Corpus c2(dir);
    c2.load_existing();
    CHECK(c2.size() == 2);
    fs::remove_all(dir);
}

TEST("пустой корпус после загрузки из несуществующей папки") {
    auto dir = temp_dir("corpus_missing");
    fs::remove_all(dir.parent_path() / "does_not_exist");
    Corpus c(dir);
    CHECK(c.empty());
    fs::remove_all(dir);
}

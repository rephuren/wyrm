// Параллельный фаззинг: воркеры-процессы с общей папкой корпуса.
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "test_framework.hpp"
#include "wyrm/corpus.hpp"
#include "wyrm/parallel.hpp"

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

fs::path temp_dir(const char* name) {
    fs::path p = fs::temp_directory_path() / (std::string("wyrm_par_") + name);
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

size_t count_files(const fs::path& dir, const std::string& prefix) {
    size_t n = 0;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().rfind(prefix, 0) == 0) ++n;
    return n;
}

}  // namespace

TEST("корпус: файлы других воркеров подхватываются один раз, свои не возвращаются") {
    auto dir = temp_dir("sync");
    Corpus a(dir, "w0-"), b(dir, "w1-");
    a.add({1, 1, 1});
    a.add({2, 2, 2});
    auto first = b.scan_new();
    CHECK(first.size() == 2);
    CHECK(b.scan_new().empty());
    b.add({3, 3, 3});
    auto for_a = a.scan_new();
    CHECK(for_a.size() == 1 && for_a[0] == Bytes({3, 3, 3}));
    CHECK(a.scan_new().empty());
    fs::remove_all(dir);
}

TEST("корпус: временные файлы чужой записи игнорируются, пока не переименованы") {
    auto dir = temp_dir("tmpfiles");
    Corpus c(dir, "w0-");
    std::ofstream(dir / ".w1-seed-0.bin.tmp") << "half";
    CHECK(c.scan_new().empty());
    fs::remove_all(dir);
}

TEST("корпус: после перезапуска нумерация продолжается и старые входы не затираются") {
    auto dir = temp_dir("numbering");
    {
        Corpus c(dir);
        c.add({1});
        c.add({2});
    }
    Corpus again(dir);
    again.add({3});
    CHECK(fs::exists(dir / "seed-0.bin") && fs::exists(dir / "seed-1.bin") && fs::exists(dir / "seed-2.bin"));
    fs::remove_all(dir);
}

TEST("parallel: два воркера находят баги и делят бюджет запусков") {
    auto dir = temp_dir("run");
    fs::create_directories(dir / "seeds");
    for (auto [name, bytes] : std::vector<std::pair<const char*, Bytes>>{{"o", {'O', 1, 2, 3}}, {"d", {'D', 1, 2, 3}}, {"r", {'R', 1, 2, 3, 4, 5}}}) {
        std::ofstream f(dir / "seeds" / name, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    Options o;
    o.target_path = target("target_vuln_buffer").string();
    o.corpus_dir = (dir / "corpus").string();
    o.crashes_dir = (dir / "crashes").string();
    o.max_runs = 12000;
    o.jobs = 2;
    o.seed = 11;
    o.sync_interval = 0.2;
    ParallelResult r = run_parallel(o, (dir / "seeds").string(), nullptr, nullptr);
    CHECK(r.workers == 2);
    CHECK(r.stats.runs == 12000);
    CHECK(r.stats.crashes_found >= 2);
    CHECK(!r.crashes.empty());
    for (const auto& c : r.crashes) CHECK(fs::exists(c.file));
    CHECK(count_files(dir / "crashes", "crash-w0-") + count_files(dir / "crashes", "crash-w1-") > 0);
    CHECK(count_files(dir / "corpus", "seed-") == 3);  // сиды положил один процесс, а не по копии на воркера
    fs::remove_all(dir);
}

TEST("parallel: остановка снаружи завершает всех воркеров") {
    auto dir = temp_dir("stop");
    Options o;
    o.target_path = target("target_json_lite").string();
    o.corpus_dir = (dir / "corpus").string();
    o.crashes_dir = (dir / "crashes").string();
    o.jobs = 3;
    auto t0 = std::chrono::steady_clock::now();
    ParallelResult r = run_parallel(o, "", nullptr, [&] {
        return std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(800);
    });
    double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.reason.empty());
    CHECK(took < 15.0);
    CHECK(r.stats.runs > 0);
    fs::remove_all(dir);
}

TEST("parallel: ошибка воркера (нет цели) возвращается исключением, а не зависанием") {
    auto dir = temp_dir("err");
    Options o;
    o.target_path = "/нет/такой/цели";
    o.corpus_dir = (dir / "corpus").string();
    o.crashes_dir = (dir / "crashes").string();
    o.jobs = 2;
    o.max_runs = 10;
    fs::create_directories(dir / "corpus");
    std::ofstream(dir / "corpus" / "seed-0.bin") << "x";
    bool threw = false;
    try {
        run_parallel(o, "", nullptr, nullptr);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    fs::remove_all(dir);
}

#include "wyrm/fuzzer.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace wyrm {

Fuzzer::Fuzzer(Options opts)
    : opts_(std::move(opts)), executor_(opts_, coverage_), mutator_(opts_.seed ? opts_.seed : std::random_device{}()),
      corpus_(opts_.corpus_dir) {
    fs::create_directories(opts_.crashes_dir);
}

void Fuzzer::seed_from_dir(const std::string& dir) {
    if (!fs::exists(dir)) return;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream f(entry.path(), std::ios::binary);
        if (!f) continue;
        Bytes data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        add_seed(std::move(data));
    }
}

void Fuzzer::add_seed(Bytes data) {
    RunResult r = executor_.run(data);
    corpus_.add(std::move(data));  // начальные сиды держим в корпусе всегда, даже без нового покрытия
    if (r.status == RunResult::kCrash) handle_result(corpus_.entries().back(), r);
}

std::string Fuzzer::extract_reason(const std::string& text) const {
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("ERROR:") != std::string::npos || line.find("runtime error:") != std::string::npos)
            return line.size() > 160 ? line.substr(0, 160) : line;
    }
    return text.empty() ? "(нет вывода от санитайзера)" : text.substr(0, std::min<size_t>(160, text.size()));
}

void Fuzzer::handle_result(const Bytes& input, const RunResult& r) {
    ++stats_.crashes_found;
    fs::path path = fs::path(opts_.crashes_dir) / ("crash-" + std::to_string(stats_.crashes_found) + ".bin");
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(input.data()), static_cast<std::streamsize>(input.size()));
    CrashRecord rec;
    rec.file = path.string();
    rec.reason = extract_reason(r.crash_output);
    rec.status = r.status;
    rec.signal = r.signal;
    crashes_.push_back(std::move(rec));
}

std::string Fuzzer::run(const std::function<void(const Stats&)>& progress, const std::function<bool()>& should_stop) {
    if (corpus_.empty()) corpus_.add(mutator_.random_bytes(1, 32));
    start_ = std::chrono::steady_clock::now();

    while (true) {
        if (opts_.max_runs >= 0 && stats_.runs >= opts_.max_runs) return "max_runs";
        stats_.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
        if (opts_.max_seconds >= 0 && stats_.elapsed >= opts_.max_seconds) return "max_seconds";
        if (should_stop && should_stop()) return "";

        const Bytes& base = corpus_.pick(mutator_.rng());
        std::uniform_int_distribution<int> splice_chance(0, 4);
        Bytes candidate = (splice_chance(mutator_.rng()) == 0 && corpus_.size() > 1)
                              ? mutator_.mutate(mutator_.splice(base, corpus_.pick(mutator_.rng()), opts_.max_input_size),
                                               opts_.max_input_size)
                              : mutator_.mutate(base, opts_.max_input_size);

        RunResult r = executor_.run(candidate);
        ++stats_.runs;

        if (r.status == RunResult::kCrash) {
            handle_result(candidate, r);
        } else if (r.status == RunResult::kOk && r.new_edges > 0) {
            corpus_.add(std::move(candidate));
        }
        stats_.corpus_size = corpus_.size();
        stats_.edges_covered = coverage_.total_edges();
        stats_.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
        stats_.exec_per_sec = stats_.elapsed > 0 ? static_cast<double>(stats_.runs) / stats_.elapsed : 0.0;
        if (progress) progress(stats_);
    }
}

}  // namespace wyrm

#include "wyrm/corpus.hpp"

#include <algorithm>
#include <fstream>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;

namespace wyrm {

namespace {
Bytes read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
}  // namespace

Corpus::Corpus(fs::path dir, std::string prefix) : dir_(std::move(dir)), prefix_(std::move(prefix)) {
    fs::create_directories(dir_);
    // Нумерация продолжается после уже лежащих файлов: новый вход не затирает старый, даже если корпус не загружали.
    const std::string stem = prefix_ + "seed-";
    for (const auto& entry : fs::directory_iterator(dir_)) {
        std::string name = entry.path().filename().string();
        if (name.rfind(stem, 0) != 0) continue;
        try {
            next_id_ = std::max(next_id_, static_cast<size_t>(std::stoul(name.substr(stem.size()))) + 1);
        } catch (...) {
        }
    }
}

void Corpus::load_existing() {
    for (auto& data : scan_new()) add_external(std::move(data));
}

std::vector<Bytes> Corpus::scan_new() {
    std::vector<Bytes> fresh;
    if (!fs::exists(dir_)) return fresh;
    for (const auto& entry : fs::directory_iterator(dir_)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;  // временные файлы чужой записи ещё не готовы
        if (!seen_.insert(name).second) continue;
        fresh.push_back(read_file(entry.path()));
    }
    return fresh;
}

const Bytes& Corpus::add(Bytes data) {
    std::string name = prefix_ + "seed-" + std::to_string(next_id_++) + ".bin";
    fs::path path = dir_ / name;
    fs::path tmp = dir_ / ("." + name + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) throw std::runtime_error("не удалось записать файл корпуса: " + path.string());
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    fs::rename(tmp, path);  // атомарно: другой воркер не увидит недописанный файл
    seen_.insert(name);
    entries_.push_back(std::move(data));
    return entries_.back();
}

const Bytes& Corpus::add_external(Bytes data) {
    entries_.push_back(std::move(data));
    return entries_.back();
}

const Bytes& Corpus::pick(std::mt19937_64& rng) const {
    if (entries_.empty()) throw std::runtime_error("корпус пуст");
    std::uniform_int_distribution<size_t> dist(0, entries_.size() - 1);
    return entries_[dist(rng)];
}

}  // namespace wyrm

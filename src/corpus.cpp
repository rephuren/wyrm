#include "wyrm/corpus.hpp"

#include <fstream>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;

namespace wyrm {

Corpus::Corpus(fs::path dir) : dir_(std::move(dir)) { fs::create_directories(dir_); }

void Corpus::load_existing() {
    if (!fs::exists(dir_)) return;
    for (const auto& entry : fs::directory_iterator(dir_)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream f(entry.path(), std::ios::binary);
        if (!f) continue;
        Bytes data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        entries_.push_back(std::move(data));
        ++next_id_;
    }
}

const Bytes& Corpus::add(Bytes data) {
    fs::path path = dir_ / ("seed-" + std::to_string(next_id_++) + ".bin");
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("не удалось записать файл корпуса: " + path.string());
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    entries_.push_back(std::move(data));
    return entries_.back();
}

const Bytes& Corpus::pick(std::mt19937_64& rng) const {
    if (entries_.empty()) throw std::runtime_error("корпус пуст");
    std::uniform_int_distribution<size_t> dist(0, entries_.size() - 1);
    return entries_[dist(rng)];
}

}  // namespace wyrm

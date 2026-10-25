#pragma once
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "wyrm/types.hpp"

namespace wyrm {

// Входы, которые дали новое покрытие. Хранятся на диске как файлы (seed-N.bin),
// чтобы прогресс переживал перезапуск (--resume).
class Corpus {
public:
    explicit Corpus(std::filesystem::path dir);

    void load_existing();
    const Bytes& add(Bytes data);  // сохраняет на диск, возвращает ссылку на добавленный элемент
    const Bytes& pick(std::mt19937_64& rng) const;
    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    const std::vector<Bytes>& entries() const { return entries_; }

private:
    std::filesystem::path dir_;
    std::vector<Bytes> entries_;
    size_t next_id_ = 0;
};

}  // namespace wyrm

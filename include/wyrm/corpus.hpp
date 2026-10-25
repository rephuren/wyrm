#pragma once
#include <filesystem>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "wyrm/types.hpp"

namespace wyrm {

// Входы, которые дали новое покрытие. Хранятся на диске как файлы (<префикс>seed-N.bin),
// чтобы прогресс переживал перезапуск (--resume), а воркеры параллельного фаззинга обменивались находками
// через общую папку: каждый пишет файлы со своим префиксом и подбирает чужие.
class Corpus {
public:
    explicit Corpus(std::filesystem::path dir, std::string prefix = "");

    void load_existing();
    const Bytes& add(Bytes data);  // сохраняет на диск (атомарно), возвращает ссылку на добавленный элемент
    // Файлы в папке, которых этот корпус ещё не видел (чужие находки и прошлые запуски): без записи на диск.
    std::vector<Bytes> scan_new();
    const Bytes& add_external(Bytes data);  // в память, без записи: файл уже лежит на диске
    const Bytes& pick(std::mt19937_64& rng) const;
    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    const std::vector<Bytes>& entries() const { return entries_; }

private:
    std::filesystem::path dir_;
    std::string prefix_;
    std::vector<Bytes> entries_;
    std::unordered_set<std::string> seen_;
    size_t next_id_ = 0;
};

}  // namespace wyrm

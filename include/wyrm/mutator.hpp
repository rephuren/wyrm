#pragma once
#include <cstdint>
#include <random>

#include "wyrm/types.hpp"

namespace wyrm {

// Мутации в духе AFL: побитовые/побайтовые флипы, арифметика, интересные константы,
// вставка/удаление кусков, сращивание с другим входом из корпуса.
class Mutator {
public:
    explicit Mutator(uint64_t seed);

    Bytes mutate(const Bytes& input, size_t max_size);
    Bytes splice(const Bytes& a, const Bytes& b, size_t max_size);
    Bytes random_bytes(size_t min_len, size_t max_len);

    std::mt19937_64& rng() { return rng_; }

private:
    std::mt19937_64 rng_;

    void bitflip(Bytes& b);
    void byteflip(Bytes& b);
    void arith(Bytes& b);
    void interesting(Bytes& b);
    void insert_chunk(Bytes& b, size_t max_size);
    void delete_chunk(Bytes& b);
    void overwrite_chunk(Bytes& b);
};

}  // namespace wyrm

#include "wyrm/mutator.hpp"

#include <algorithm>

namespace wyrm {

namespace {
constexpr int8_t kInteresting8[] = {-128, -1, 0, 1, 2, 16, 32, 64, 100, 127};
constexpr int16_t kInteresting16[] = {-32768, -129, 128, 255, 256, 512, 1000, 1024, 4096, 32767};
constexpr int32_t kInteresting32[] = {-2147483648, -100663046, -32769, 32768, 65535, 65536, 100663045, 2147483647};
}  // namespace

Mutator::Mutator(uint64_t seed) : rng_(seed) {}

Bytes Mutator::random_bytes(size_t min_len, size_t max_len) {
    std::uniform_int_distribution<size_t> len_dist(min_len, std::max(min_len, max_len));
    size_t len = len_dist(rng_);
    Bytes out(len);
    std::uniform_int_distribution<int> byte_dist(0, 255);
    for (auto& b : out) b = static_cast<uint8_t>(byte_dist(rng_));
    return out;
}

Bytes Mutator::mutate(const Bytes& input, size_t max_size) {
    Bytes out = input;
    if (out.empty()) out = random_bytes(1, 16);

    // Несколько проходов за одну мутацию, как в havoc-режиме AFL.
    std::uniform_int_distribution<int> rounds_dist(1, 6);
    int rounds = rounds_dist(rng_);
    for (int i = 0; i < rounds; ++i) {
        std::uniform_int_distribution<int> op_dist(0, 6);
        switch (op_dist(rng_)) {
            case 0: bitflip(out); break;
            case 1: byteflip(out); break;
            case 2: arith(out); break;
            case 3: interesting(out); break;
            case 4: if (out.size() < max_size) insert_chunk(out, max_size); break;
            case 5: delete_chunk(out); break;
            case 6: overwrite_chunk(out); break;
        }
        if (out.empty()) out = random_bytes(1, 8);
    }
    if (out.size() > max_size) out.resize(max_size);
    return out;
}

Bytes Mutator::splice(const Bytes& a, const Bytes& b, size_t max_size) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::uniform_int_distribution<size_t> cut_a(0, a.size() - 1);
    std::uniform_int_distribution<size_t> cut_b(0, b.size() - 1);
    size_t ca = cut_a(rng_), cb = cut_b(rng_);
    Bytes out(a.begin(), a.begin() + static_cast<long>(ca));
    out.insert(out.end(), b.begin() + static_cast<long>(cb), b.end());
    if (out.size() > max_size) out.resize(max_size);
    return out;
}

void Mutator::bitflip(Bytes& b) {
    std::uniform_int_distribution<size_t> pos_dist(0, b.size() - 1);
    std::uniform_int_distribution<int> bit_dist(0, 7);
    b[pos_dist(rng_)] ^= static_cast<uint8_t>(1u << bit_dist(rng_));
}

void Mutator::byteflip(Bytes& b) {
    std::uniform_int_distribution<size_t> pos_dist(0, b.size() - 1);
    b[pos_dist(rng_)] = static_cast<uint8_t>(~b[pos_dist(rng_)]);
}

void Mutator::arith(Bytes& b) {
    std::uniform_int_distribution<size_t> pos_dist(0, b.size() - 1);
    std::uniform_int_distribution<int> delta_dist(-35, 35);
    size_t pos = pos_dist(rng_);
    b[pos] = static_cast<uint8_t>(static_cast<int>(b[pos]) + delta_dist(rng_));
}

void Mutator::interesting(Bytes& b) {
    std::uniform_int_distribution<int> which(0, 2);
    switch (which(rng_)) {
        case 0: {
            std::uniform_int_distribution<size_t> pos_dist(0, b.size() - 1);
            std::uniform_int_distribution<size_t> val_dist(0, sizeof(kInteresting8) / sizeof(kInteresting8[0]) - 1);
            b[pos_dist(rng_)] = static_cast<uint8_t>(kInteresting8[val_dist(rng_)]);
            break;
        }
        case 1: {
            if (b.size() < 2) return;
            std::uniform_int_distribution<size_t> pos_dist(0, b.size() - 2);
            std::uniform_int_distribution<size_t> val_dist(0, sizeof(kInteresting16) / sizeof(kInteresting16[0]) - 1);
            int16_t v = kInteresting16[val_dist(rng_)];
            size_t pos = pos_dist(rng_);
            b[pos] = static_cast<uint8_t>(v & 0xff);
            b[pos + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
            break;
        }
        default: {
            if (b.size() < 4) return;
            std::uniform_int_distribution<size_t> pos_dist(0, b.size() - 4);
            std::uniform_int_distribution<size_t> val_dist(0, sizeof(kInteresting32) / sizeof(kInteresting32[0]) - 1);
            int32_t v = kInteresting32[val_dist(rng_)];
            size_t pos = pos_dist(rng_);
            for (int i = 0; i < 4; ++i) b[pos + static_cast<size_t>(i)] = static_cast<uint8_t>((v >> (8 * i)) & 0xff);
        }
    }
}

void Mutator::insert_chunk(Bytes& b, size_t max_size) {
    std::uniform_int_distribution<size_t> pos_dist(0, b.size());
    std::uniform_int_distribution<size_t> len_dist(1, std::min<size_t>(16, max_size > b.size() ? max_size - b.size() : 1));
    Bytes chunk = random_bytes(1, len_dist(rng_));
    b.insert(b.begin() + static_cast<long>(pos_dist(rng_)), chunk.begin(), chunk.end());
}

void Mutator::delete_chunk(Bytes& b) {
    if (b.size() <= 1) return;
    std::uniform_int_distribution<size_t> len_dist(1, std::min<size_t>(16, b.size() - 1));
    size_t len = len_dist(rng_);
    std::uniform_int_distribution<size_t> pos_dist(0, b.size() - len);
    size_t pos = pos_dist(rng_);
    b.erase(b.begin() + static_cast<long>(pos), b.begin() + static_cast<long>(pos + len));
}

void Mutator::overwrite_chunk(Bytes& b) {
    std::uniform_int_distribution<size_t> len_dist(1, std::min<size_t>(16, b.size()));
    size_t len = len_dist(rng_);
    std::uniform_int_distribution<size_t> pos_dist(0, b.size() - len);
    size_t pos = pos_dist(rng_);
    Bytes chunk = random_bytes(len, len);
    std::copy(chunk.begin(), chunk.end(), b.begin() + static_cast<long>(pos));
}

}  // namespace wyrm

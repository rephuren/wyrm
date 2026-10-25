#include "wyrm/minimizer.hpp"

namespace wyrm {

namespace {

// Один проход delta-debugging: пробует выкинуть вход по кусочкам убывающего размера.
bool ddmin_pass(Bytes& data, const std::function<bool(const Bytes&)>& still_crashes) {
    bool changed_any = false;
    size_t chunk = data.size() / 2;
    while (chunk > 0) {
        bool changed = true;
        while (changed && data.size() > 0) {
            changed = false;
            for (size_t start = 0; start < data.size(); start += chunk) {
                size_t end = std::min(start + chunk, data.size());
                Bytes candidate;
                candidate.reserve(data.size() - (end - start));
                candidate.insert(candidate.end(), data.begin(), data.begin() + static_cast<long>(start));
                candidate.insert(candidate.end(), data.begin() + static_cast<long>(end), data.end());
                if (!candidate.empty() && still_crashes(candidate)) {
                    data = std::move(candidate);
                    changed = true;
                    changed_any = true;
                    break;
                }
            }
        }
        chunk /= 2;
    }
    return changed_any;
}

// После удаления блоков пробует занулить оставшиеся байты по одному: часто превращает
// случайные значения в нули, которые легче читать в отчёте.
void zero_pass(Bytes& data, const std::function<bool(const Bytes&)>& still_crashes) {
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] == 0) continue;
        uint8_t orig = data[i];
        data[i] = 0;
        if (!still_crashes(data)) data[i] = orig;
    }
}

}  // namespace

Bytes minimize(const Bytes& crashing_input, const std::function<bool(const Bytes&)>& still_crashes, int max_rounds) {
    Bytes data = crashing_input;
    for (int round = 0; round < max_rounds; ++round) {
        if (!ddmin_pass(data, still_crashes)) break;
    }
    zero_pass(data, still_crashes);
    return data;
}

}  // namespace wyrm

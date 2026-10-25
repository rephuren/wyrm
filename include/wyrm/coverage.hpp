#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wyrm {

// Общая память с байтовой картой покрытия (как у AFL). Фаззер создаёт карту,
// передаёт её имя цели через переменную окружения WYRM_SHM_ID; цель подключается
// к ней из coverage_rt.cpp через __sanitizer_cov_trace_pc_guard*.
class CoverageMap {
public:
    static constexpr size_t kSize = 1 << 16;  // 64 КБ: достаточно гвардов для среднего таргета без коллизий

    CoverageMap();
    ~CoverageMap();
    CoverageMap(const CoverageMap&) = delete;
    CoverageMap& operator=(const CoverageMap&) = delete;

    const std::string& shm_name() const { return name_; }
    uint8_t* data() { return map_; }
    const uint8_t* data() const { return map_; }

    void clear();
    // Число байтов, впервые ставших ненулевыми относительно accumulated_. Обновляет accumulated_.
    size_t merge_new_edges();
    size_t total_edges() const { return edge_count_; }

private:
    std::string name_;
    int fd_ = -1;
    uint8_t* map_ = nullptr;
    std::vector<uint8_t> accumulated_;
    size_t edge_count_ = 0;
};

}  // namespace wyrm

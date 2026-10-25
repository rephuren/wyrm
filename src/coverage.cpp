#include "wyrm/coverage.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace wyrm {

CoverageMap::CoverageMap() : accumulated_(kSize, 0) {
    char tmpl[] = "/tmp/wyrm_cov_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) throw std::runtime_error("mkstemp для карты покрытия не удался");
    if (ftruncate(fd, static_cast<off_t>(kSize)) != 0) {
        close(fd);
        throw std::runtime_error("ftruncate для карты покрытия не удался");
    }
    void* addr = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("mmap для карты покрытия не удался");
    }
    fd_ = fd;
    map_ = static_cast<uint8_t*>(addr);
    name_ = tmpl;
    clear();
}

CoverageMap::~CoverageMap() {
    if (map_) munmap(map_, kSize);
    if (fd_ >= 0) close(fd_);
    if (!name_.empty()) unlink(name_.c_str());
}

void CoverageMap::clear() { std::memset(map_, 0, kSize); }

size_t CoverageMap::merge_new_edges() {
    size_t added = 0;
    for (size_t i = 0; i < kSize; ++i) {
        if (map_[i] != 0 && accumulated_[i] == 0) {
            accumulated_[i] = 1;
            ++added;
            ++edge_count_;
        }
    }
    return added;
}

}  // namespace wyrm

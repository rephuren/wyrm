// Среда выполнения покрытия для целей фаззинга: собирается прямо в каждую цель.
// Подключается к общей памяти по имени из WYRM_SHM_ID и отмечает байт карты при каждом
// проходе через инструментированную точку перехода (AFL-style edge hashing).
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>

namespace {
uint8_t* g_map = nullptr;
uint32_t g_prev_loc = 0;
constexpr size_t kMapSize = 1 << 16;

uint8_t* map() {
    if (g_map) return g_map;
    const char* shm_id = getenv("WYRM_SHM_ID");
    if (!shm_id) return nullptr;
    int fd = open(shm_id, O_RDWR);
    if (fd < 0) return nullptr;
    void* addr = mmap(nullptr, kMapSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (addr == MAP_FAILED) return nullptr;
    g_map = static_cast<uint8_t*>(addr);
    return g_map;
}
}  // namespace

extern "C" void __sanitizer_cov_trace_pc_guard_init(uint32_t* start, uint32_t* stop) {
    if (start == stop || *start) return;
    static uint32_t counter = 1;
    for (uint32_t* p = start; p < stop; ++p) *p = counter++;
}

extern "C" void __sanitizer_cov_trace_pc_guard(uint32_t* guard) {
    uint8_t* m = map();
    if (!m) return;
    uint32_t loc = *guard & (kMapSize - 1);
    m[loc ^ g_prev_loc]++;
    g_prev_loc = loc >> 1;
}

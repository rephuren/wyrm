// main() для целей фаззинга. Цель реализует LLVMFuzzerTestOneInput, а этот файл решает, как её вызывать:
//  - обычный запуск: один вход из stdin или из файла argv[1], один вызов, выход (fork+exec на вход);
//  - fork-сервер (WYRM_FORKSERVER=1): процесс один раз прогревается и на каждый вход делает fork(); потомок выполняет
//    вход и выходит, поэтому состояние между входами не накапливается, а exec и повторная инициализация не нужны;
//  - persistent (WYRM_PERSISTENT=1): процесс живёт долго, фаззер присылает входы через общую память и
//    команды по каналу, цель вызывается в цикле без fork+exec. Состояние между вызовами не сбрасывается,
//    как и в libFuzzer/AFL++ persistent: цель должна быть чистой по отношению к своим глобалам.
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "wyrm/persistent.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
extern "C" void wyrm_reset_coverage_state();

namespace {

bool read_full(int fd, void* buf, size_t n) {
    auto* p = static_cast<uint8_t*>(buf);
    while (n > 0) {
        ssize_t r = read(fd, p, n);
        if (r <= 0) return false;
        p += r;
        n -= static_cast<size_t>(r);
    }
    return true;
}

int run_standalone(int argc, char** argv) {
    int fd = STDIN_FILENO;
    if (argc > 1) {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
            std::perror("open");
            return 2;
        }
    }
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) data.insert(data.end(), buf, buf + n);
    LLVMFuzzerTestOneInput(data.data(), data.size());
    return 0;
}

// Общая память входа: [длина:4][данные]. Возвращает указатель на начало или nullptr.
uint8_t* map_input() {
    const char* shm = getenv(wyrm::kInputShmEnv);
    if (!shm) return nullptr;
    int fd = open(shm, O_RDWR);
    if (fd < 0) return nullptr;
    void* addr = mmap(nullptr, wyrm::kInputShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return addr == MAP_FAILED ? nullptr : static_cast<uint8_t*>(addr);
}

bool write_full(int fd, const void* buf, size_t n) {
    auto* p = static_cast<const uint8_t*>(buf);
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

int run_forkserver() {
    uint8_t* base = map_input();
    if (!base) return 2;
    const uint8_t* input = base + sizeof(uint32_t);
    uint32_t hello = 1;
    if (!write_full(wyrm::kStatusFd, &hello, sizeof(hello))) return 0;

    uint32_t cmd;
    while (read_full(wyrm::kCtlFd, &cmd, sizeof(cmd))) {  // EOF = фаззер просит завершиться
        pid_t child = fork();
        if (child < 0) return 3;
        if (child == 0) {
            close(wyrm::kCtlFd);
            close(wyrm::kStatusFd);
            uint32_t len = *reinterpret_cast<volatile uint32_t*>(base);
            if (len > wyrm::kMaxPersistentInput) len = wyrm::kMaxPersistentInput;
            wyrm_reset_coverage_state();
            LLVMFuzzerTestOneInput(input, len);
            _exit(0);
        }
        uint32_t pid32 = static_cast<uint32_t>(child);
        if (!write_full(wyrm::kStatusFd, &pid32, sizeof(pid32))) {
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            return 0;
        }
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
        uint32_t word = static_cast<uint32_t>(status);
        if (!write_full(wyrm::kStatusFd, &word, sizeof(word))) return 0;
    }
    return 0;
}

int run_persistent() {
    const char* shm = getenv(wyrm::kInputShmEnv);
    if (!shm) return 2;
    int fd = open(shm, O_RDWR);
    if (fd < 0) return 2;
    void* addr = mmap(nullptr, wyrm::kInputShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (addr == MAP_FAILED) return 2;
    auto* base = static_cast<uint8_t*>(addr);
    const uint8_t* input = base + sizeof(uint32_t);

    uint32_t cmd;
    while (read_full(wyrm::kCtlFd, &cmd, sizeof(cmd))) {  // EOF = фаззер просит завершиться
        uint32_t len = *reinterpret_cast<volatile uint32_t*>(base);
        if (len > wyrm::kMaxPersistentInput) len = wyrm::kMaxPersistentInput;
        wyrm_reset_coverage_state();
        LLVMFuzzerTestOneInput(input, len);
        uint32_t done = 1;
        if (write(wyrm::kStatusFd, &done, sizeof(done)) != static_cast<ssize_t>(sizeof(done))) return 0;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (getenv(wyrm::kForkserverEnv)) return run_forkserver();
    if (getenv(wyrm::kPersistentEnv)) return run_persistent();
    return run_standalone(argc, argv);
}

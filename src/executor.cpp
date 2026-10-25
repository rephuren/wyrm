#include "wyrm/executor.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace wyrm {

namespace {
constexpr size_t kMaxCrashOutput = 8192;

std::string read_all(int fd, size_t limit) {
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<size_t>(n));
        if (out.size() >= limit) break;
    }
    return out;
}
}  // namespace

Executor::Executor(const Options& opts, CoverageMap& coverage) : opts_(opts), coverage_(coverage) {
    shm_env_ = "WYRM_SHM_ID=" + coverage_.shm_name();
}

RunResult Executor::run(const Bytes& input) {
    coverage_.clear();
    RunResult result;

    std::string tmp_input_path;
    if (!opts_.use_stdin) {
        tmp_input_path = "/tmp/wyrm_in_XXXXXX";
        std::vector<char> tmpl(tmp_input_path.begin(), tmp_input_path.end());
        tmpl.push_back('\0');
        int fd = mkstemp(tmpl.data());
        if (fd < 0) throw std::runtime_error("mkstemp для входного файла не удался");
        write(fd, input.data(), input.size());
        close(fd);
        tmp_input_path.assign(tmpl.data());
    }

    int stdin_pipe[2] = {-1, -1};
    int stderr_pipe[2];
    if (opts_.use_stdin && pipe(stdin_pipe) != 0) throw std::runtime_error("pipe() не удался");
    if (pipe(stderr_pipe) != 0) throw std::runtime_error("pipe() не удался");

    auto start = std::chrono::steady_clock::now();
    pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("fork() не удался");

    if (pid == 0) {
        // дочерний процесс: цель
        if (opts_.use_stdin) {
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        } else {
            close(STDIN_FILENO);
        }
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDOUT_FILENO);

        putenv(const_cast<char*>(shm_env_.c_str()));
        // Детерминированный ASan: один процесс на вход, повторный краш не нужен, а без этого
        // abort_on_error иногда роняет процесс по-другому при повторном запуске.
        setenv("ASAN_OPTIONS", "abort_on_error=1:exitcode=134:detect_leaks=0", 1);
        setenv("UBSAN_OPTIONS", "abort_on_error=1:exitcode=134", 1);

        if (opts_.use_stdin) {
            execl(opts_.target_path.c_str(), opts_.target_path.c_str(), nullptr);
        } else {
            execl(opts_.target_path.c_str(), opts_.target_path.c_str(), tmp_input_path.c_str(), nullptr);
        }
        _exit(127);  // execl не вернулся бы при успехе
    }

    // родитель
    if (opts_.use_stdin) {
        close(stdin_pipe[0]);
        // Пишем без блокировки навсегда: если цель не читает стандартный ввод, просто закрываем.
        fcntl(stdin_pipe[1], F_SETFL, O_NONBLOCK);
        size_t off = 0;
        while (off < input.size()) {
            ssize_t n = write(stdin_pipe[1], input.data() + off, input.size() - off);
            if (n <= 0) break;
            off += static_cast<size_t>(n);
        }
        close(stdin_pipe[1]);
    }
    close(stderr_pipe[1]);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    bool timed_out = false;
    int status = 0;
    const auto deadline = start + std::chrono::duration<double>(opts_.timeout_seconds);
    for (;;) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (std::chrono::steady_clock::now() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            timed_out = true;
            break;
        }
        usleep(500);
    }

    result.crash_output = read_all(stderr_pipe[0], kMaxCrashOutput);
    close(stderr_pipe[0]);
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (!tmp_input_path.empty()) unlink(tmp_input_path.c_str());

    if (timed_out) {
        result.status = RunResult::kTimeout;
    } else if (WIFSIGNALED(status)) {
        result.status = RunResult::kCrash;
        result.signal = WTERMSIG(status);
    } else {
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        result.exit_code = code;
        // ASAN_OPTIONS=exitcode=134 выше: явный код краша, который exit() не спутает со сбойным,
        // но не аварийным завершением пользовательского кода.
        result.status = (code == 134) ? RunResult::kCrash : RunResult::kOk;
    }
    result.new_edges = coverage_.merge_new_edges();
    return result;
}

}  // namespace wyrm

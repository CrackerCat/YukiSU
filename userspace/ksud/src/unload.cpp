#include "unload.hpp"
#include "../kagami/include/kagami/kasumi_client.hpp"
#include "core/ksucalls.hpp"
#include "log.hpp"
#include "utils.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <vector>

namespace ksud {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

bool is_ksu_fd(std::string_view target) {
    return target.find("[ksu_driver") != std::string_view::npos ||
           target.find("[ksu_fdwrapper]") != std::string_view::npos ||
           target.find("[ksu_sulog]") != std::string_view::npos ||
           target.find("[ksu_su_prompt]") != std::string_view::npos ||
           target.find("[yukizygisk_exit]") != std::string_view::npos;
}

std::vector<int> ksu_fds(const std::filesystem::path& process, std::error_code& error) {
    std::vector<int> result;
    for (auto it = std::filesystem::directory_iterator(process / "fd", error);
         !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        std::error_code link_error;
        const auto target = std::filesystem::read_symlink(it->path(), link_error);
        if (link_error || !is_ksu_fd(target.string()))
            continue;
        const auto name = it->path().filename().string();
        int fd = -1;
        const auto [end, ec] = std::from_chars(name.data(), name.data() + name.size(), fd);
        if (ec == std::errc{} && end == name.data() + name.size() && fd >= 0)
            result.push_back(fd);
    }
    return result;
}

class ProcessWaiter {
public:
    ProcessWaiter() = default;
    ProcessWaiter(const ProcessWaiter&) = delete;
    ProcessWaiter& operator=(const ProcessWaiter&) = delete;
    ProcessWaiter(ProcessWaiter&&) = delete;
    ProcessWaiter& operator=(ProcessWaiter&&) = delete;
    ~ProcessWaiter() {
        for (const auto& entry : processes)
            if (entry.fd >= 0)
                close(entry.fd);
    }

    void add(int pidfd) { processes.push_back({pidfd, POLLIN, 0}); }

    bool wait() {
        const auto deadline = Clock::now() + 30s;
        size_t remaining = processes.size();
        while (remaining) {
            const auto time_left =
                std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now());
            if (time_left.count() <= 0)
                return false;
            const int ready =
                poll(processes.data(), processes.size(), static_cast<int>(time_left.count()));
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready <= 0)
                return false;
            for (auto& entry : processes) {
                if (entry.fd < 0 || !entry.revents)
                    continue;
                if (!(entry.revents & (POLLIN | POLLHUP)))
                    return false;
                close(entry.fd);
                entry.fd = -1;
                --remaining;
            }
        }
        return true;
    }

private:
    std::vector<pollfd> processes;
};

bool stop_ksu_processes() {
    ProcessWaiter waiter;
    std::error_code error;
    for (auto it = std::filesystem::directory_iterator("/proc", error);
         !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        const auto name = it->path().filename().string();
        pid_t pid = 0;
        const auto [end, ec] = std::from_chars(name.data(), name.data() + name.size(), pid);
        if (ec != std::errc{} || end != name.data() + name.size() || pid <= 1 || pid == getpid())
            continue;

        // Bind the signal to this process before reading its domain or descriptors.
        const int pidfd = static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
        if (pidfd < 0) {
            if (errno == ESRCH)
                continue;
            LOGE("Cannot open pidfd for %d: %s", pid, strerror(errno));
            return false;
        }
        const auto context = read_file((it->path() / "attr/current").string());
        std::error_code fd_error;
        const bool holder = !ksu_fds(it->path(), fd_error).empty();
        auto domain = context ? trim_view(*context) : std::string_view{};
        if (!domain.empty() && domain.back() == '\0')
            domain.remove_suffix(1);
        if (!holder && domain != "u:r:ksu:s0") {
            close(pidfd);
            continue;
        }
        if (syscall(SYS_pidfd_send_signal, pidfd, SIGKILL, nullptr, 0) != 0) {
            const int signal_error = errno;
            close(pidfd);
            if (signal_error == ESRCH)
                continue;
            LOGE("Cannot stop process %d: %s", pid, strerror(signal_error));
            return false;
        }
        waiter.add(pidfd);
    }
    if (error) {
        LOGE("Cannot enumerate processes: %s", error.message().c_str());
        return false;
    }
    if (!waiter.wait()) {
        LOGE("KernelSU processes did not exit before the unload deadline");
        return false;
    }
    return true;
}

bool remove_kernel_module() {
    if (!stop_ksu_processes())
        return false;
    std::error_code error;
    const auto fds = ksu_fds("/proc/self", error);
    if (error) {
        LOGE("Cannot enumerate own descriptors: %s", error.message().c_str());
        return false;
    }
    for (const int fd : fds)
        close(fd);
    if (syscall(SYS_delete_module, "kernelsu", O_NONBLOCK) != 0) {
        LOGE("Cannot unload kernelsu: %s", strerror(errno));
        return false;
    }
    return true;
}
}  // namespace

int unload() {
    std::string version_error;
    if (!ensure_uapi_version_matched(&version_error)) {
        LOGE("Cannot unload KernelSU: %s", version_error.c_str());
        return 1;
    }
    if (!is_lkm()) {
        LOGE("KernelSU is built into the kernel and cannot be unloaded");
        return 1;
    }
    if (!(get_flags() & KSU_GET_INFO_FLAG_SAFE_UNLOAD)) {
        LOGE("This kernel does not support safe unloading; update the kernel or use a full reboot");
        return 1;
    }
    const auto kasumi = kagami::kasumi::version_info();
    if (kasumi.status != kagami::kasumi::Status::NotPresent) {
        LOGE("Kasumi has initialized and pins the kernel module; use a full reboot");
        return 1;
    }
    if (kasumi.last_errno != ENODEV && kasumi.last_errno != ENOTTY &&
        kasumi.last_errno != EOPNOTSUPP) {
        LOGE("Cannot determine Kasumi runtime state; refusing to unload");
        return 1;
    }
    detach_process_group(true);
    switch_cgroups();
    if (!switch_mnt_ns(1))
        return 1;
    if (signal(SIGHUP, SIG_IGN) == SIG_ERR || signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        LOGE("Cannot protect unload from shell termination: %s", strerror(errno));
        return 1;
    }

    LOGI("Stopping Android services for KernelSU unload");
    const auto stopped = exec_command({"/system/bin/stop"}, 30s);
    bool unloaded = false;
    if (stopped.exit_code == 0) {
        unloaded = remove_kernel_module();
    } else {
        LOGE("Cannot stop Android services: %s", stopped.stderr_str.c_str());
    }
    // Restart even when stopping services or removing the module failed.
    const auto started = exec_command({"/system/bin/start"}, 30s);
    if (started.exit_code != 0)
        LOGE("Cannot restart Android services: %s", started.stderr_str.c_str());
    return unloaded && started.exit_code == 0 ? 0 : 1;
}
}  // namespace ksud

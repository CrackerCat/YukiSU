#include "preinit.hpp"
#include "../core/ksucalls.hpp"
#include "../core/restorecon.hpp"
#include "../defs.hpp"
#include "../log.hpp"
#include "../utils.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <set>

namespace ksud::preinit {
namespace {
namespace fs = std::filesystem;
constexpr uint64_t kBlock = 4096;
constexpr uint64_t kLimit = 4U << 20;
constexpr uint64_t kReserve = 8U << 20;
constexpr uint64_t kBootcountLimit = 3;
constexpr const char* kManifest = ".manifest";
constexpr const char* kBootcount = ".preinit_bootcount";
constexpr const char* kStatusPath = "/data/adb/ksu/preinit_status";

struct Entry {
    fs::path relative;
    bool directory{};
    uint64_t size{};
    timespec ctime{};
    mode_t mode{};
};

struct Candidate {
    std::string id;
    fs::path source;
    std::vector<Entry> entries;
    uint64_t usage = kBlock;
    std::string manifest;
};

struct Status {
    std::string state;
    std::string message;
};

bool valid_id(const std::string& id) {
    return !id.empty() && id.front() != '.' &&
           id.find_first_not_of(
               "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
               std::string::npos;
}

bool path_exists(const fs::path& path) {
    std::error_code error;
    return fs::exists(fs::symlink_status(path, error));
}

bool directory(const fs::path& path) {
    std::error_code error;
    return fs::is_directory(fs::symlink_status(path, error));
}

bool remove_path(const fs::path& path) {
    std::error_code error;
    fs::remove_all(path, error);
    if (error)
        LOGW("preinit: remove %s: %s", path.c_str(), error.message().c_str());
    return !error;
}

bool secure(const fs::path& path, mode_t mode, const char* context) {
    if (lchown(path.c_str(), 0, 0) || chmod(path.c_str(), mode & 0755))
        return false;
    if (!lsetfilecon(path, context))
        LOGW("preinit: could not label %s", path.c_str());
    return true;
}

bool sync_path(const fs::path& path) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;
    const bool synced = fsync(fd) == 0;
    const bool closed = close(fd) == 0;
    return synced && closed;
}

bool scan(const fs::path& root, const fs::path& relative, Candidate& candidate, std::string& error,
          unsigned depth = 0) {
    if (depth > 32 || candidate.entries.size() > 4096) {
        error = "too many preinit entries";
        return false;
    }
    std::error_code ec;
    std::vector<fs::path> paths;
    for (fs::directory_iterator it(root / relative, ec), end; it != end && !ec; it.increment(ec))
        paths.push_back(relative / it->path().filename());
    if (ec) {
        error = ec.message();
        return false;
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
        struct stat st{};
        if (path.native().find_first_of("\t\r\n") != std::string::npos ||
            lstat((root / path).c_str(), &st) != 0 ||
            (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))) {
            error = "unsupported file: " + path.string();
            return false;
        }
        const bool is_dir = S_ISDIR(st.st_mode);
        const auto size = is_dir ? 0 : static_cast<uint64_t>(st.st_size);
        const uint64_t blocks =
            is_dir ? kBlock : ((std::min(size, kLimit + kBlock) + kBlock - 1) / kBlock) * kBlock;
        candidate.usage = std::min(kLimit + kBlock, candidate.usage + blocks);
        candidate.entries.push_back({path, is_dir, size, st.st_ctim, st.st_mode & 07777});
        char metadata[128];
        const int length = snprintf(metadata, sizeof(metadata), "\t%c\t%llu\t%lld.%ld\t%o\t",
                                    is_dir ? 'd' : 'f', static_cast<unsigned long long>(size),
                                    static_cast<long long>(st.st_ctim.tv_sec), st.st_ctim.tv_nsec,
                                    static_cast<unsigned>(st.st_mode & 07777));
        if (length < 0 || static_cast<size_t>(length) >= sizeof(metadata)) {
            error = "invalid preinit metadata";
            return false;
        }
        candidate.manifest += candidate.id + metadata + path.string() + '\n';
        if (is_dir && !scan(root, path, candidate, error, depth + 1))
            return false;
    }
    return true;
}

uint64_t disk_usage(const fs::path& path) {
    struct stat st{};
    if (lstat(path.c_str(), &st))
        return 0;
    uint64_t total = static_cast<uint64_t>(st.st_blocks) * 512;
    if (S_ISDIR(st.st_mode)) {
        std::error_code ec;
        for (fs::directory_iterator it(path, ec), end; it != end && !ec; it.increment(ec))
            total += disk_usage(it->path());
    }
    return total;
}

bool available(const fs::path& path, uint64_t& bytes) {
    struct statvfs st{};
    if (statvfs(path.c_str(), &st))
        return false;
    bytes = static_cast<uint64_t>(st.f_bavail) * st.f_frsize;
    return true;
}

bool same_copy(const fs::path& destination, const Candidate& want) {
    Candidate have;
    std::string error;
    if (!directory(destination) || !scan(destination, {}, have, error) ||
        want.entries.size() != have.entries.size())
        return false;
    for (size_t i = 0; i < want.entries.size(); ++i) {
        const auto& entry = want.entries[i];
        const auto& copied = have.entries[i];
        if (entry.relative != copied.relative || entry.directory != copied.directory ||
            (entry.mode & 0755) != copied.mode || entry.size != copied.size)
            return false;
        if (!entry.directory) {
            const auto original = read_file((want.source / entry.relative).string());
            const auto current = read_file((destination / entry.relative).string());
            if (!original || !current || *original != *current)
                return false;
        }
    }
    return true;
}

bool install(const fs::path& parent, const Candidate& candidate, const char* context) {
    const fs::path temporary = parent / ('.' + candidate.id + ".tmp");
    const fs::path destination = parent / candidate.id;
    const fs::path old = parent / ('.' + candidate.id + ".old");
    if (!remove_path(temporary) || mkdir(temporary.c_str(), 0755) ||
        !secure(temporary, 0755, context))
        return false;
    for (const auto& entry : candidate.entries) {
        const fs::path target = temporary / entry.relative;
        if (entry.directory) {
            if (mkdir(target.c_str(), 0755))
                return false;
        } else {
            std::error_code error;
            fs::copy_file(candidate.source / entry.relative, target, error);
            if (error)
                return false;
        }
        if (!secure(target, entry.mode, context) || !sync_path(target))
            return false;
    }
    for (auto entry = candidate.entries.rbegin(); entry != candidate.entries.rend(); ++entry) {
        if (entry->directory && !sync_path(temporary / entry->relative))
            return false;
    }
    if (!sync_path(temporary) || !remove_path(old))
        return false;
    const bool had_old = path_exists(destination);
    if (had_old && rename(destination.c_str(), old.c_str()))
        return false;
    if (rename(temporary.c_str(), destination.c_str())) {
        if (had_old && rename(old.c_str(), destination.c_str()))
            LOGW("preinit: failed to restore %s", destination.c_str());
        return false;
    }
    const bool synced = sync_path(parent);
    return remove_path(old) && synced;
}

bool remove_unlisted(const fs::path& path, const std::set<std::string>& keep, bool& changed) {
    std::error_code ec;
    for (fs::directory_iterator it(path, ec), end; it != end && !ec; it.increment(ec)) {
        const auto name = it->path().filename().string();
        if (name == kManifest || keep.count(name))
            continue;
        if (!remove_path(it->path()))
            return false;
        changed = changed || name.front() != '.';
    }
    return !ec;
}

bool remove_preinit(const fs::path& dir) {
    const bool removed = remove_path(dir / "preinit");
    return remove_path(dir / kBootcount) && removed;
}

void write_status(const std::map<std::string, Status>& states) {
    std::string content;
    for (const auto& [id, status] : states) {
        std::string message = status.message;
        std::replace(message.begin(), message.end(), '\n', ' ');
        std::replace(message.begin(), message.end(), '\r', ' ');
        content.append(id)
            .append(" ")
            .append(status.state)
            .append(" ")
            .append(message)
            .append("\n");
    }
    if (!ensure_dir_exists(WORKING_DIR) || !write_file_atomic(kStatusPath, content))
        LOGW("preinit: failed to write status");
}

uint64_t read_bootcount() {
    auto content = read_file((storage_dir() / kBootcount).string());
    uint64_t count = 0;
    if (content)
        std::from_chars(content->data(), content->data() + content->size(), count);
    return count;
}
}  // namespace

std::filesystem::path storage_dir() {
    if (get_uapi_version() >= 6 && directory("/metadata/vold"))
        return PREINIT_DIR_VOLD;
    return directory("/metadata/watchdog") ? PREINIT_DIR_WATCHDOG : PREINIT_DIR_DEFAULT;
}

const char* file_context(const std::filesystem::path& dir) {
    return dir == PREINIT_DIR_VOLD ? "u:object_r:vold_metadata_file:s0"
                                   : "u:object_r:metadata_file:s0";
}

int sync(const std::filesystem::path& dir, const std::vector<std::filesystem::path>& stale,
         const std::map<std::string, std::filesystem::path>& modules, bool supported) {
    bool ok = true;
    for (const auto& path : stale)
        ok = remove_preinit(path) && ok;
    std::map<std::string, Status> states;
    std::vector<Candidate> candidates;
    for (const auto& [id, module] : modules) {
        const fs::path source = module / "preinit";
        if (!path_exists(source))
            continue;
        if (!valid_id(id))
            continue;
        if (!supported) {
            states[id] = {"unsupported", "kernel does not run preinit"};
            continue;
        }
        Candidate candidate{id, source, {}, kBlock, {}};
        std::string error;
        struct stat st{};
        if (!directory(source) || lstat((source / "preinit.conf").c_str(), &st) ||
            !S_ISREG(st.st_mode)) {
            error = "preinit.conf not found or not a regular file";
        } else if (scan(source, {}, candidate, error)) {
            candidate.usage += ((candidate.manifest.size() + kBlock - 1) / kBlock) * kBlock;
            candidates.push_back(std::move(candidate));
            continue;
        }
        states[id] = {"error", error};
    }
    if (candidates.empty()) {
        ok = remove_preinit(dir) && ok;
        write_status(states);
        return ok ? 0 : 1;
    }
    const fs::path target = dir / "preinit";
    const char* context = file_context(dir);
    uint64_t free = 0;
    if (!directory(dir) || !secure(dir, 0755, context) ||
        (path_exists(target) && !directory(target)) || !ensure_dir_exists(target.string()) ||
        !secure(target, 0755, context) || !available(dir, free)) {
        for (const auto& candidate : candidates)
            states[candidate.id] = {"error", "cannot prepare preinit storage"};
        write_status(states);
        return 1;
    }
    free += disk_usage(target);
    const uint64_t budget = std::min(kLimit, free > kReserve ? free - kReserve : 0);
    uint64_t used = 2 * kBlock;
    std::set<std::string> wanted;
    for (const auto& candidate : candidates) {
        if (used > budget || candidate.usage > budget - used) {
            states[candidate.id] = {"no_space", "preinit storage budget exhausted"};
        } else {
            used += candidate.usage;
            wanted.insert(candidate.id);
        }
    }
    bool changed = false;
    if (!remove_unlisted(target, wanted, changed))
        ok = false;
    std::set<std::string> keep;
    std::string manifest;
    for (const auto& candidate : candidates) {
        if (!wanted.count(candidate.id))
            continue;
        const fs::path destination = target / candidate.id;
        if (!same_copy(destination, candidate)) {
            if (!available(dir, free)) {
                states[candidate.id] = {"error", "cannot read free space"};
                continue;
            }
            if (free < candidate.usage + kReserve && path_exists(destination)) {
                if (remove_path(destination))
                    changed = true;
            }
            if (!available(dir, free) || free < candidate.usage + kReserve) {
                states[candidate.id] = {"no_space", "insufficient free space"};
                continue;
            }
            if (!install(target, candidate, context)) {
                states[candidate.id] = {"error", "failed to copy preinit content"};
                continue;
            }
            changed = true;
        }
        keep.insert(candidate.id);
        manifest += candidate.manifest;
        states[candidate.id] = {"ok", {}};
    }
    ok = remove_unlisted(target, keep, changed) && ok;
    ok = write_file_atomic(target / kManifest, manifest) && ok;
    ok = secure(target / kManifest, 0644, context) && ok;
    ok = sync_path(target) && ok;
    if (changed) {
        ok = remove_path(dir / kBootcount) && ok;
        ok = sync_path(dir) && ok;
    }
    if (!ok) {
        for (auto& [id, status] : states) {
            if (status.state == "ok")
                status = {"error", "failed to persist preinit state"};
        }
    }
    write_status(states);
    return ok ? 0 : 1;
}

std::map<std::string, std::string> module_states() {
    std::map<std::string, std::string> result;
    const bool bootloop = read_bootcount() > kBootcountLimit;
    const auto content = read_file(kStatusPath);
    if (!content)
        return result;
    for_each_line(*content, [&](std::string_view line) {
        const auto first = line.find(' ');
        if (first == std::string_view::npos)
            return;
        const auto tail = line.substr(first + 1);
        const auto state = tail.substr(0, tail.find(' '));
        result[std::string(line.substr(0, first))] =
            bootloop && state == "ok" ? "bootloop" : std::string(state);
    });
    return result;
}

void warn_status(const std::string& id) {
    const auto states = module_states();
    const auto it = states.find(id);
    if (it != states.end() && it->second != "ok")
        printf("! preinit will not take effect: %s\n", it->second.c_str());
}

void on_boot_completed() {
    const auto count = read_bootcount();
    if (count > kBootcountLimit) {
        LOGW("preinit: disabled after repeated failed boots; run ksud module preinit reset");
    } else if (count > 0) {
        reset_bootcount();
    }
}

int reset_bootcount() {
    const auto dir = storage_dir();
    if (!path_exists(dir / kBootcount))
        return 0;
    return remove_path(dir / kBootcount) && sync_path(dir) ? 0 : 1;
}

int print_status() {
    const auto count = read_bootcount();
    if (count > kBootcountLimit)
        printf("preinit: disabled after 3 failed boots (reset with `ksud module preinit reset`)\n");
    else
        printf("preinit: bootcount %llu/3\n", static_cast<unsigned long long>(count));
    const auto content = read_file(kStatusPath);
    if (content && fputs(content->c_str(), stdout) == EOF)
        return 1;
    return 0;
}

}  // namespace ksud::preinit

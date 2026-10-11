#include "dynamic_manager.hpp"
#include "boot/apk_sign.hpp"
#include "core/json.hpp"
#include "defs.hpp"
#include "log.hpp"
#include "terminal.hpp"
#include "utils.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>

namespace ksud {

namespace {

bool parse_size(const std::string& value, uint32_t* out) {
    if (value.empty() || value.find('\0') != std::string::npos || value.front() == '-' ||
        value.front() == '+' || std::isspace(static_cast<unsigned char>(value.front())))
        return false;

    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 0);
    if (end == value.c_str() || *end != '\0' || errno == ERANGE ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    *out = static_cast<uint32_t>(parsed);
    return true;
}

bool parse_size(const json::Value& value, uint32_t* out) {
    if (value.type == json::Type::Number) {
        if (!std::isfinite(value.n) || std::floor(value.n) != value.n || value.n < 0 ||
            value.n > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        *out = static_cast<uint32_t>(value.n);
        return true;
    }

    if (value.type != json::Type::String) {
        return false;
    }
    return parse_size(value.s, out);
}

bool normalize_hash(const std::string& value, char out[65]) {
    if (value.size() != 64) {
        return false;
    }

    for (size_t i = 0; i < value.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (!std::isxdigit(c)) {
            return false;
        }
        out[i] = static_cast<char>(std::tolower(c));
    }
    out[64] = '\0';
    return true;
}

bool normalize_hash(const json::Value& value, char out[65]) {
    return value.type == json::Type::String && normalize_hash(value.s, out);
}

bool same_sign(const DynamicManagerSign& lhs, const DynamicManagerSign& rhs) {
    return lhs.size == rhs.size && std::strcmp(lhs.hash, rhs.hash) == 0;
}

class ConfigLock {
public:
    ConfigLock() {
        if (ensure_dir_exists(WORKING_DIR)) {
            fd_ = open((std::string(DYNAMIC_MANAGER_CONFIG_PATH) + ".lock").c_str(),
                       O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
            locked_ = fd_ >= 0 && flock(fd_, LOCK_EX) == 0;
        }
    }
    ~ConfigLock() {
        if (fd_ >= 0)
            close(fd_);
    }
    ConfigLock(const ConfigLock&) = delete;
    ConfigLock& operator=(const ConfigLock&) = delete;
    ConfigLock(ConfigLock&&) = delete;
    ConfigLock& operator=(ConfigLock&&) = delete;
    [[nodiscard]] bool locked() const { return locked_; }

private:
    int fd_ = -1;
    bool locked_ = false;
};

bool parse_compat(const json::Value& value, ksu_dynamic_manager_compat* compat) {
    if (value.type != json::Type::Object)
        return false;
    if (value.contains("version") && !parse_size(value.at("version"), &compat->version))
        return false;
    if (value.contains("uapi_version") &&
        !parse_size(value.at("uapi_version"), &compat->uapi_version))
        return false;
    if (value.contains("version_full")) {
        const auto& name = value.at("version_full");
        if (name.type != json::Type::String || name.s.size() >= sizeof(compat->version_full) ||
            name.s.find('\0') != std::string::npos)
            return false;
        std::strcpy(compat->version_full, name.s.c_str());
    }
    return true;
}

bool append_entries(const json::Value& value, std::vector<DynamicManagerEntry>* entries) {
    if (value.type == json::Type::Array) {
        return std::all_of(value.a.begin(), value.a.end(),
                           [&](const auto& item) { return append_entries(item, entries); });
    }
    if (value.type != json::Type::Object)
        return false;
    if (value.contains("size") || value.contains("hash")) {
        DynamicManagerEntry entry{};
        if (!parse_size(value.at("size"), &entry.sign.size) || !entry.sign.size ||
            !normalize_hash(value.at("hash"), entry.sign.hash) ||
            (value.contains("compat") && !parse_compat(value.at("compat"), &entry.compat)))
            return false;
        if (entries->size() >= KSU_DYNAMIC_MANAGER_MAX_SIGNS ||
            std::any_of(entries->begin(), entries->end(),
                        [&](const auto& existing) { return same_sign(existing.sign, entry.sign); }))
            return false;
        entries->push_back(entry);
        return true;
    }
    for (const char* key : {"managers", "dynamic_managers", "signs"}) {
        if (value.contains(key))
            return append_entries(value.at(key), entries);
    }
    return false;
}

std::string entry_to_json(const DynamicManagerEntry& entry) {
    std::string result = "{\"size\":\"";
    append_hex(&result, entry.sign.size);
    result += "\",\"hash\":\"";
    result += entry.sign.hash;
    result += "\",\"compat\":{\"version\":" + std::to_string(entry.compat.version);
    result += ",\"uapi_version\":" + std::to_string(entry.compat.uapi_version);
    result += ",\"version_full\":" + json::escape_string(entry.compat.version_full) + "}}";
    return result;
}

std::string entries_to_json(const std::vector<DynamicManagerEntry>& entries) {
    std::string result = "[";
    for (const auto& entry : entries) {
        if (result.size() > 1)
            result += ',';
        result += '\n';
        result += entry_to_json(entry);
    }
    return result + "\n]\n";
}

std::optional<std::vector<DynamicManagerEntry>> load_config() {
    std::error_code error;
    const bool exists = std::filesystem::exists(DYNAMIC_MANAGER_CONFIG_PATH, error);
    if (!error && !exists)
        return std::vector<DynamicManagerEntry>{};
    const auto content = read_file(DYNAMIC_MANAGER_CONFIG_PATH);
    std::vector<DynamicManagerEntry> entries;
    if (error || !content || !append_entries(json::parse(*content), &entries)) {
        terminal::error("Invalid or unreadable dynamic manager configuration");
        return std::nullopt;
    }
    return entries;
}

int apply_config(const std::vector<DynamicManagerEntry>& entries) {
    if (set_dynamic_manager_config(entries) != 0) {
        terminal::errorf("Failed to apply dynamic manager configuration: %s (errno %d)",
                         strerror(errno), errno);
        return 1;
    }
    return 0;
}

int save_config(const std::vector<DynamicManagerEntry>& entries,
                const std::vector<DynamicManagerEntry>& previous) {
    if (apply_config(entries) != 0)
        return 1;
    if (!write_file_atomic(DYNAMIC_MANAGER_CONFIG_PATH, entries_to_json(entries))) {
        terminal::file_error("write", DYNAMIC_MANAGER_CONFIG_PATH, errno);
        if (apply_config(previous) != 0)
            terminal::error("Failed to restore the previous dynamic manager configuration");
        return 1;
    }
    const int directory = open(WORKING_DIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const bool synced = directory >= 0 && fsync(directory) == 0;
    if (directory >= 0)
        close(directory);
    if (!synced) {
        terminal::error("Failed to sync dynamic manager configuration");
        if (!write_file_atomic(DYNAMIC_MANAGER_CONFIG_PATH, entries_to_json(previous)))
            terminal::error("Failed to restore the persisted dynamic manager configuration");
        if (apply_config(previous) != 0)
            terminal::error("Failed to restore the previous dynamic manager configuration");
        return 1;
    }
    return 0;
}

bool parse_compat_args(const std::vector<std::string>& args, size_t start,
                       ksu_dynamic_manager_compat* compat) {
    for (size_t i = start; i < args.size(); i += 2) {
        if (i + 1 == args.size())
            return false;
        const auto& value = args[i + 1];
        if (args[i] == "--version") {
            if (!parse_size(value, &compat->version))
                return false;
        } else if (args[i] == "--uapi") {
            if (!parse_size(value, &compat->uapi_version))
                return false;
        } else if (args[i] == "--full-version") {
            if (value.size() >= sizeof(compat->version_full) ||
                value.find('\0') != std::string::npos)
                return false;
            std::strcpy(compat->version_full, value.c_str());
        } else {
            return false;
        }
    }
    return true;
}

void print_help() {
    printf("USAGE: ksud dynamic <SUBCOMMAND>\n\n");
    printf("SUBCOMMANDS:\n");
    printf("  get-sign [--json] <APK>   Get APK v2 signature without applying it\n");
    printf("  get-sign [--json] --uid <UID>\n");
    printf("  set-hash <size> <hash>   Persist and apply a size/hash signature\n");
    printf("  set-apk <APK>            Extract APK v2 signature, persist, and apply\n");
    printf("  set-uid <UID>            Extract signature from an installed UID's APK\n");
    printf("  get-config --uid <UID>   Read the signature and compatibility settings as JSON\n");
    printf("  list [--json]            List persisted dynamic manager settings\n");
    printf("  Set options: --version <code> --uapi <version> --full-version <name>\n");
    printf("  Zero/empty compatibility values use the actual kernel values.\n");
    printf("  del <size> <hash>        Delete a persisted size/hash and apply\n");
    printf("  clear                    Clear persisted signatures and apply\n");
    printf("  help                     Show this help\n");
}

const char* bool_json(bool value) {
    return value ? "true" : "false";
}

void print_apk_signature(const ApkSignatureInfo& info, bool json) {
    if (json) {
        printf("{\n");
        printf("  \"v1\": %s,\n", bool_json(info.v1));
        printf("  \"v2\": {\n");
        printf("    \"has\": %s,\n", bool_json(info.v2.has));
        printf("    \"hash\": \"%s\",\n", info.v2.hash.c_str());
        printf("    \"size\": \"0x%x\"\n", info.v2.size);
        printf("  },\n");
        printf("  \"v3\": %s,\n", bool_json(info.v3));
        printf("  \"v3.1\": %s,\n", bool_json(info.v31));
        printf("  \"manager_error\": %s,\n", json::escape_string(info.manager_error).c_str());
        printf("  \"manager_error_detail\": %s\n",
               json::escape_string(info.manager_error_detail).c_str());
        printf("}\n");
        return;
    }

    if (info.v2.has) {
        printf("size: 0x%x\n", info.v2.size);
        printf("hash: %s\n", info.v2.hash.c_str());
    }
    if (info.v1 || info.v3 || info.v31) {
        terminal::message(stderr, "warning", "Found v1/v3/v3.1 sign data");
    }
    if (!info.v2.has) {
        terminal::message(stderr, "warning", "v2 sign data not found");
    }
    if (!info.manager_error.empty())
        terminal::message(stderr, "warning", manager_apk_error_message(info));
}

int print_apk_signature(const std::string& apk, bool json) {
    const auto info = get_apk_signature(apk);
    if (!info.valid && !json) {
        terminal::errorf("Failed to get APK signature: %s\n", apk.c_str());
        return 1;
    }

    print_apk_signature(info, json);
    return info.v2.has ? 0 : 1;
}

bool sign_from_hash_args(const std::string& size_arg, const std::string& hash_arg,
                         DynamicManagerSign* sign) {
    DynamicManagerSign parsed{};
    if (!parse_size(size_arg, &parsed.size)) {
        terminal::errorf("Invalid size: %s\n", size_arg.c_str());
        return false;
    }
    if (!normalize_hash(hash_arg, parsed.hash)) {
        terminal::errorf("Invalid hash: %s\n", hash_arg.c_str());
        return false;
    }

    *sign = parsed;
    return true;
}

bool sign_from_apk(const std::string& apk, DynamicManagerSign* sign) {
    const auto info = get_apk_signature(apk);
    if (!info.manager_error.empty()) {
        terminal::errorf("%s: %s", apk.c_str(), manager_apk_error_message(info).c_str());
        return false;
    }

    DynamicManagerSign parsed{};
    parsed.size = info.v2.size;
    if (!normalize_hash(info.v2.hash, parsed.hash)) {
        terminal::errorf("Invalid v2 hash extracted from APK: %s\n", apk.c_str());
        return false;
    }

    *sign = parsed;
    return true;
}

std::vector<std::string> packages_for_uid(uint32_t uid, uint32_t user_id) {
    std::vector<std::string> packages;
    const std::string user = std::to_string(user_id);
    ExecResult result = exec_command({"cmd", "package", "list", "packages", "--user", user, "-U"});
    if (result.exit_code != 0 || result.stdout_str.empty()) {
        result = exec_command({"pm", "list", "packages", "--user", user, "-U"});
    }
    if (result.exit_code != 0 || result.stdout_str.empty()) {
        result = exec_command({"cmd", "package", "list", "packages", "-U"});
    }
    if (result.exit_code != 0 || result.stdout_str.empty()) {
        result = exec_command({"pm", "list", "packages", "-U"});
    }

    for (const auto& raw_line : split(result.stdout_str, '\n')) {
        const std::string line = trim(raw_line);
        if (line.empty()) {
            continue;
        }

        const size_t pkg_pos = line.find("package:");
        const size_t uid_pos = line.find("uid:");
        if (pkg_pos == std::string::npos || uid_pos == std::string::npos) {
            continue;
        }

        const size_t pkg_start = pkg_pos + strlen("package:");
        size_t pkg_end = line.find_first_of(" \t\r\n", pkg_start);
        if (pkg_end == std::string::npos) {
            pkg_end = line.size();
        }

        uint32_t parsed_uid = 0;
        const size_t uid_start = uid_pos + strlen("uid:");
        size_t uid_end = line.find_first_of(" \t\r\n", uid_start);
        if (uid_end == std::string::npos) {
            uid_end = line.size();
        }
        if (!parse_size(line.substr(uid_start, uid_end - uid_start), &parsed_uid)) {
            continue;
        }

        if (parsed_uid == uid) {
            packages.push_back(line.substr(pkg_start, pkg_end - pkg_start));
        }
    }

    return packages;
}

std::vector<std::string> apk_paths_for_package(const std::string& package_name, uint32_t user_id) {
    std::vector<std::string> paths;
    const std::string user = std::to_string(user_id);
    ExecResult result = exec_command({"cmd", "package", "path", "--user", user, package_name});
    if (result.exit_code != 0 || result.stdout_str.empty()) {
        result = exec_command({"pm", "path", "--user", user, package_name});
    }
    if (result.exit_code != 0 || result.stdout_str.empty()) {
        result = exec_command({"cmd", "package", "path", package_name});
    }
    if (result.exit_code != 0 || result.stdout_str.empty()) {
        result = exec_command({"pm", "path", package_name});
    }

    for (const auto& raw_line : split(result.stdout_str, '\n')) {
        std::string line = trim(raw_line);
        if (!starts_with(line, "package:")) {
            continue;
        }
        line.erase(0, strlen("package:"));
        if (!line.empty()) {
            paths.push_back(line);
        }
    }

    std::stable_sort(paths.begin(), paths.end(),
                     [](const std::string& lhs, const std::string& rhs) {
                         return ends_with(lhs, "/base.apk") && !ends_with(rhs, "/base.apk");
                     });
    return paths;
}

bool sign_from_uid(uint32_t uid, DynamicManagerSign* sign, bool quiet = false) {
    constexpr uint32_t kPerUserRange = 100000;
    const uint32_t user_id = uid / kPerUserRange;
    const auto packages = packages_for_uid(uid, user_id);
    if (packages.empty()) {
        printf("No package found for uid: %u\n", uid);
        return false;
    }

    for (const auto& package_name : packages) {
        const auto paths = apk_paths_for_package(package_name, user_id);
        for (const auto& path : paths) {
            if (!starts_with(path, "/data/app/") || !ends_with(path, "/base.apk")) {
                terminal::errorf("APK is outside the kernel manager scan path: %s", path.c_str());
                continue;
            }
            DynamicManagerSign parsed{};
            if (sign_from_apk(path, &parsed)) {
                if (!quiet) {
                    printf("package: %s\n", package_name.c_str());
                    printf("apk: %s\n", path.c_str());
                }
                *sign = parsed;
                return true;
            }
        }
    }

    terminal::errorf("No compatible manager APK found for uid: %u", uid);
    return false;
}

bool apk_from_uid(uint32_t uid, std::string* package_out, std::string* apk_out) {
    constexpr uint32_t kPerUserRange = 100000;
    const uint32_t user_id = uid / kPerUserRange;
    const auto packages = packages_for_uid(uid, user_id);
    if (packages.empty()) {
        printf("No package found for uid: %u\n", uid);
        return false;
    }

    std::string fallback_package;
    std::string fallback_apk;
    for (const auto& package_name : packages) {
        const auto paths = apk_paths_for_package(package_name, user_id);
        for (const auto& path : paths) {
            if (!ends_with(path, "/base.apk")) {
                continue;
            }
            if (fallback_apk.empty()) {
                fallback_package = package_name;
                fallback_apk = path;
            }
            if (!starts_with(path, "/data/app/") ||
                !get_apk_signature(path).manager_error.empty()) {
                continue;
            }
            *package_out = package_name;
            *apk_out = path;
            return true;
        }
    }

    // Preserve diagnostics when none of the UID's APKs can be authorized.
    if (!fallback_apk.empty()) {
        *package_out = fallback_package;
        *apk_out = fallback_apk;
        return true;
    }

    terminal::errorf("No base APK found for uid: %u", uid);
    return false;
}

int add_sign(const DynamicManagerSign& sign, const std::vector<std::string>& args,
             size_t options_start) {
    const auto previous = load_config();
    if (!previous)
        return 1;
    auto entries = *previous;
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&](const auto& entry) { return same_sign(entry.sign, sign); });
    if (it == entries.end()) {
        if (entries.size() >= KSU_DYNAMIC_MANAGER_MAX_SIGNS)
            return terminal::error("Too many dynamic manager signatures");
        entries.push_back({sign, {}});
        it = entries.end() - 1;
    }
    if (!parse_compat_args(args, options_start, &it->compat))
        return terminal::error("Invalid dynamic manager compatibility options");
    if (save_config(entries, *previous) != 0)
        return 1;
    printf("%s\n", entry_to_json(*it).c_str());
    return 0;
}

}  // namespace

int load_and_apply_dynamic_managers() {
    const ConfigLock lock;
    if (!lock.locked())
        return terminal::error("Failed to lock dynamic manager configuration");
    const auto entries = load_config();
    return entries ? apply_config(*entries) : 1;
}

int cmd_dynamic_manager(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "help" || args[0] == "-h" || args[0] == "--help") {
        print_help();
        return args.empty() ? 1 : 0;
    }

    const std::string& subcmd = args[0];

    if (subcmd == "get-sign") {
        bool json = false;
        bool uid_mode = false;
        std::string target;

        for (size_t i = 1; i < args.size(); i++) {
            if (args[i] == "--") {
                if (!target.empty() || i + 2 != args.size())
                    return terminal::error("expected one APK path after --");
                target = args[i + 1];
                break;
            }
            if (args[i] == "--json") {
                json = true;
            } else if (args[i] == "--uid" && i + 1 < args.size()) {
                uid_mode = true;
                target = args[++i];
            } else if (target.empty()) {
                target = args[i];
            } else {
                printf("Unexpected argument: %s\n", args[i].c_str());
                return 1;
            }
        }

        if (target.empty()) {
            printf("USAGE: ksud dynamic get-sign [--json] <APK>\n");
            printf("       ksud dynamic get-sign [--json] --uid <UID>\n");
            return 1;
        }

        if (uid_mode) {
            uint32_t uid = 0;
            if (!parse_size(target, &uid)) {
                terminal::errorf("Invalid uid: %s\n", target.c_str());
                return 1;
            }

            std::string package_name;
            std::string apk;
            if (!apk_from_uid(uid, &package_name, &apk)) {
                return 1;
            }
            if (!json) {
                printf("package: %s\n", package_name.c_str());
                printf("apk: %s\n", apk.c_str());
            }
            return print_apk_signature(apk, json);
        }

        return print_apk_signature(target, json);
    }

    const ConfigLock lock;
    if (!lock.locked())
        return terminal::error("Failed to lock dynamic manager configuration");

    if (subcmd == "get-config" && args.size() == 3 && args[1] == "--uid") {
        uint32_t uid = 0;
        DynamicManagerEntry entry{};
        if (!parse_size(args[2], &uid) || !sign_from_uid(uid, &entry.sign, true))
            return 1;
        const auto entries = load_config();
        if (!entries)
            return 1;
        for (const auto& existing : *entries) {
            if (same_sign(existing.sign, entry.sign)) {
                entry = existing;
                break;
            }
        }
        printf("%s\n", entry_to_json(entry).c_str());
        return 0;
    }

    if (subcmd == "set-hash" && args.size() >= 3) {
        DynamicManagerSign sign{};
        return sign_from_hash_args(args[1], args[2], &sign) ? add_sign(sign, args, 3) : 1;
    }

    if (subcmd == "set-apk" && args.size() >= 2) {
        DynamicManagerSign sign{};
        return sign_from_apk(args[1], &sign) ? add_sign(sign, args, 2) : 1;
    }

    if (subcmd == "set-uid" && args.size() >= 2) {
        uint32_t uid = 0;
        if (!parse_size(args[1], &uid)) {
            terminal::errorf("Invalid uid: %s\n", args[1].c_str());
            return 1;
        }

        DynamicManagerSign sign{};
        return sign_from_uid(uid, &sign) ? add_sign(sign, args, 2) : 1;
    }

    if (subcmd == "list" && (args.size() == 1 || (args.size() == 2 && args[1] == "--json"))) {
        const auto entries = load_config();
        if (!entries)
            return 1;
        if (args.size() == 2) {
            printf("%s", entries_to_json(*entries).c_str());
        } else {
            for (const auto& entry : *entries) {
                printf("size: 0x%x\nhash: %s\nversion: %u\nuapi: %u\nfull-version: %s\n",
                       entry.sign.size, entry.sign.hash, entry.compat.version,
                       entry.compat.uapi_version, entry.compat.version_full);
            }
        }
        return 0;
    }

    if (subcmd == "del" && args.size() == 3) {
        DynamicManagerSign target{};
        if (!sign_from_hash_args(args[1], args[2], &target))
            return 1;
        const auto previous = load_config();
        if (!previous)
            return 1;
        auto entries = *previous;
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                           [&](const auto& entry) { return same_sign(entry.sign, target); }),
            entries.end());
        if (entries.size() == previous->size())
            return terminal::error("Signature not found");
        return save_config(entries, *previous);
    }

    if (subcmd == "clear" && args.size() == 1) {
        const auto previous = load_config();
        return previous ? save_config({}, *previous) : 1;
    }

    terminal::errorf("Unknown dynamic subcommand: %s\n", subcmd.c_str());
    print_help();
    return 1;
}

}  // namespace ksud

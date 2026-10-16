#include "hot_install.hpp"
#include "../defs.hpp"
#include "../log.hpp"
#include "../sepolicy/sepolicy.hpp"
#include "../utils.hpp"
#include "module.hpp"

#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <system_error>
#include <vector>

namespace ksud {
namespace {

namespace fs = std::filesystem;

bool is_boot_id(std::string_view value) {
    if (value.size() != 36)
        return false;
    for (size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (ch != '-')
                return false;
        } else if (std::string_view("0123456789abcdef").find(ch) == std::string_view::npos) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> current_boot_id() {
    const auto content = read_file("/proc/sys/kernel/random/boot_id");
    if (!content)
        return std::nullopt;
    const std::string boot_id = trim(*content);
    return is_boot_id(boot_id) ? std::make_optional(boot_id) : std::nullopt;
}

bool check_run_script(const fs::path& staged_dir, const std::string& script,
                      std::optional<fs::path>& result, std::string& reason) {
    result.reset();
    if (script.empty())
        return true;
    const fs::path relative(script);
    if (script.find('\0') != std::string::npos || relative.is_absolute() ||
        std::any_of(relative.begin(), relative.end(),
                    [](const fs::path& component) { return component == ".."; })) {
        reason = "invalid MODULE_HOT_RUN_SCRIPT";
        return false;
    }

    const fs::path path = staged_dir / relative;
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory ||
        status.type() == fs::file_type::not_found) {
        printf("! MODULE_HOT_RUN_SCRIPT '%s' not found, skipping it\n", script.c_str());
        return true;
    }
    const fs::path resolved = error ? fs::path{} : fs::canonical(path, error);
    if (error) {
        reason = "Cannot resolve hot install script: " + error.message();
        return false;
    }
    const fs::path root = fs::canonical(staged_dir, error);
    if (error || !fs::is_regular_file(resolved, error) ||
        std::mismatch(root.begin(), root.end(), resolved.begin(), resolved.end()).first !=
            root.end()) {
        reason = "MODULE_HOT_RUN_SCRIPT is not a file inside the module";
        return false;
    }
    result = resolved.lexically_relative(root);
    return true;
}

bool swap_module(const fs::path& staged_dir, const fs::path& live_dir, const fs::path& trash,
                 std::string& reason) {
    fs::path stashed;
    std::error_code error;
    const bool has_live = fs::exists(live_dir, error);
    if (error) {
        reason = "Cannot inspect live module: " + error.message();
        return false;
    }
    if (has_live) {
        const auto boot_id = current_boot_id();
        if (!boot_id) {
            reason = "Cannot determine the current boot for hot install trash";
            return false;
        }
        const fs::path generation = trash / *boot_id;
        fs::create_directories(generation, error);
        if (error) {
            reason = "Cannot create hot install trash: " + error.message();
            return false;
        }
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        stashed = generation / (live_dir.filename().string() + "." + std::to_string(getpid()) +
                                "." + std::to_string(stamp));
        if (fs::exists(stashed, error) || error) {
            reason = "hot install trash destination is unavailable";
            return false;
        }
        fs::rename(live_dir, stashed, error);
        if (error) {
            reason = "Cannot move live module aside: " + error.message();
            return false;
        }
    }
    fs::rename(staged_dir, live_dir, error);
    if (!error)
        return true;
    if (!stashed.empty()) {
        std::error_code restore_error;
        fs::rename(stashed, live_dir, restore_error);
        if (restore_error)
            LOGW("Failed to restore %s: %s", live_dir.c_str(), restore_error.message().c_str());
    }
    reason = "Cannot activate staged module: " + error.message();
    return false;
}

void preserve_legacy_trash(const fs::path& path, const fs::path& generation) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found)
        return;
    if (!error)
        fs::create_directories(generation, error);
    if (!error) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path target =
            generation / ("legacy." + std::to_string(getpid()) + "." + std::to_string(stamp));
        fs::rename(path, target, error);
    }
    if (error)
        LOGW("Failed to preserve legacy hot install trash %s: %s", path.c_str(),
             error.message().c_str());
}

bool remove_trash_generations(const std::vector<fs::path>& generations) {
    bool success = true;
    for (const auto& generation : generations) {
        std::error_code error;
        fs::remove_all(generation, error);
        if (error) {
            LOGW("Failed to clean %s: %s", generation.c_str(), error.message().c_str());
            success = false;
        }
    }
    return success;
}

}  // namespace

std::optional<std::string> take_hot_install_request(const fs::path& staged_dir) {
    const fs::path marker = staged_dir / HOT_INSTALL_MARKER;
    const auto content = read_file(marker.string());
    if (!content)
        return std::nullopt;
    std::error_code error;
    fs::remove(marker, error);
    if (error) {
        LOGW("Failed to remove %s: %s", marker.c_str(), error.message().c_str());
        return std::nullopt;
    }
    return trim(*content);
}

bool apply_hot_install(const std::string& module_id, bool is_metamodule,
                       const std::string& run_script) {
    printf("- Hot install requested\n");
    const fs::path staged_dir = fs::path(MODULE_UPDATE_DIR) / module_id;
    const fs::path live_dir = fs::path(MODULE_DIR) / module_id;
    std::optional<fs::path> script;
    std::string reason;
    std::error_code error;
    if (is_metamodule) {
        reason = "metamodules are always applied at boot";
    } else if (is_safe_mode()) {
        reason = "modules are disabled in safe mode";
    } else if (check_run_script(staged_dir, run_script, script, reason)) {
        for (const char* flag : {UPDATE_FILE_NAME, REMOVE_FILE_NAME, DISABLE_FILE_NAME}) {
            fs::remove(staged_dir / flag, error);
            if (error) {
                reason = "Cannot clear staged module flag: " + error.message();
                break;
            }
        }
        if (reason.empty())
            (void)swap_module(staged_dir, live_dir, HOT_INSTALL_TRASH_DIR, reason);
    }
    if (!reason.empty()) {
        printf("- Hot install skipped: %s\n- Reboot to apply the module\n", reason.c_str());
        return false;
    }

    LOGI("Module %s hot installed", module_id.c_str());
    const auto rule_file = (live_dir / "sepolicy.rule").string();
    if (fs::exists(rule_file, error) && sepolicy_apply_file(rule_file) != 0)
        printf("! Failed to apply sepolicy.rule\n");

    if (!fs::exists(live_dir / "skip_mount", error)) {
        for (const char* partition : {"system", "vendor", "product", "system_ext", "odm"}) {
            if (fs::is_directory(live_dir / partition, error)) {
                printf("- Note: partition files are mounted on the next boot\n");
                break;
            }
        }
    }
    if (script) {
        printf("- Running %s\n", script->c_str());
        const auto wait =
            ScriptWait::until(std::chrono::steady_clock::now() + HOT_INSTALL_SCRIPT_TIMEOUT);
        const int result = ksud::run_script((live_dir / *script).string(), wait, module_id, nullptr,
                                            nullptr, true);
        if (result != 0)
            printf("! Hot install script failed: %d\n", result);
    }
    return true;
}

void clean_hot_install_trash() {
    const auto boot_id = current_boot_id();
    if (!boot_id) {
        LOGW("Cannot determine the current boot, keeping hot install trash");
        return;
    }
    const fs::path trash(HOT_INSTALL_TRASH_DIR);
    const fs::path current = trash / *boot_id;
    std::vector<fs::path> stale;
    std::error_code error;
    fs::directory_iterator entry(trash, error);
    if (error == std::errc::no_such_file_or_directory)
        error.clear();
    if (error) {
        LOGW("Failed to inspect %s: %s", trash.c_str(), error.message().c_str());
        return;
    }
    const fs::directory_iterator end;
    for (; !error && entry != end; entry.increment(error)) {
        const auto& path = entry->path();
        const std::string name = path.filename().string();
        if (name == *boot_id)
            continue;
        if (is_boot_id(name)) {
            stale.push_back(path);
        } else {
            // Legacy entries may still be in use; retain them until the next boot.
            preserve_legacy_trash(path, current);
        }
    }
    if (error) {
        LOGW("Failed to scan %s: %s", trash.c_str(), error.message().c_str());
        return;
    }
    preserve_legacy_trash(fs::path(WORKING_DIR) / "hot_install_trash.old", current);
    if (stale.empty())
        return;

    const pid_t pid = fork();
    if (pid == 0) {
        detach_process_group(true);
        switch_cgroups();
        (void)reset_stdio_to_devnull();
        _exit(remove_trash_generations(stale) ? 0 : 1);
    }
    if (pid < 0)
        (void)remove_trash_generations(stale);
}

}  // namespace ksud

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
#include <system_error>

namespace ksud {
namespace {

namespace fs = std::filesystem;

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
    result = relative;
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
        fs::create_directories(trash, error);
        if (error) {
            reason = "Cannot create hot install trash: " + error.message();
            return false;
        }
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        stashed = trash / (live_dir.filename().string() + "." + std::to_string(getpid()) + "." +
                           std::to_string(stamp));
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
    const fs::path trash(HOT_INSTALL_TRASH_DIR);
    const fs::path old = fs::path(WORKING_DIR) / "hot_install_trash.old";
    std::error_code error;
    fs::remove_all(old, error);
    if (error) {
        LOGW("Failed to clean %s: %s", old.c_str(), error.message().c_str());
        return;
    }
    if (!fs::exists(trash, error))
        return;
    fs::rename(trash, old, error);
    if (error) {
        LOGW("Failed to move %s: %s", trash.c_str(), error.message().c_str());
        return;
    }

    const pid_t pid = fork();
    if (pid == 0) {
        detach_process_group(true);
        switch_cgroups();
        (void)reset_stdio_to_devnull();
        fs::remove_all(old, error);
        _exit(error ? 1 : 0);
    }
    if (pid < 0) {
        fs::remove_all(old, error);
        if (error)
            LOGW("Failed to clean %s: %s", old.c_str(), error.message().c_str());
    }
}

}  // namespace ksud

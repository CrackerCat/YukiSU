#include "profile.hpp"
#include "../defs.hpp"
#include "../log.hpp"
#include "../sepolicy/sepolicy.hpp"
#include "../terminal.hpp"
#include "../utils.hpp"

#include <dirent.h>
#include <unistd.h>

namespace ksud {

namespace {

bool is_legacy_empty_sepolicy(const std::string& package, std::string_view policy) {
    return trim_view(policy) == "No sepolicy profile for " + package;
}

int apply_profile_sepolicy(const std::string& package) {
    const auto content = read_file(std::string(PROFILE_SELINUX_DIR) + package);
    if (!content) {
        LOGE("Failed to read sepolicy profile for %s", package.c_str());
        return 1;
    }
    // Older managers persisted this diagnostic as rules. Keep the bytes for backups.
    if (is_legacy_empty_sepolicy(package, *content))
        return 0;
    return sepolicy_live_patch(*content);
}

}  // namespace

int profile_get_sepolicy(const std::string& package) {
    const std::string path = std::string(PROFILE_SELINUX_DIR) + package;
    auto content = read_file(path);
    if (content) {
        if (!is_legacy_empty_sepolicy(package, *content))
            printf("%s", content->c_str());
        return 0;
    }
    terminal::errorf("No sepolicy profile for %s\n", package.c_str());
    return 1;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) package then policy
int profile_set_sepolicy(const std::string& package, const std::string& policy) {
    if (!ensure_dir_exists(PROFILE_SELINUX_DIR))
        return 1;
    const std::string path = std::string(PROFILE_SELINUX_DIR) + package;

    if (!write_file(path, policy)) {
        LOGE("Failed to write sepolicy profile");
        return 1;
    }

    return apply_profile_sepolicy(package);
}

int profile_get_template(const std::string& id) {
    const std::string path = std::string(PROFILE_TEMPLATE_DIR) + id;
    auto content = read_file(path);
    if (content) {
        printf("%s", content->c_str());
        return 0;
    }
    terminal::errorf("Template %s not found\n", id.c_str());
    return 1;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) id then template content
int profile_set_template(const std::string& id, const std::string& template_str) {
    ensure_dir_exists(PROFILE_TEMPLATE_DIR);
    const std::string path = std::string(PROFILE_TEMPLATE_DIR) + id;

    if (!write_file(path, template_str)) {
        LOGE("Failed to write template");
        return 1;
    }

    return 0;
}

int profile_delete_template(const std::string& id) {
    const std::string path = std::string(PROFILE_TEMPLATE_DIR) + id;
    if (unlink(path.c_str()) != 0) {
        LOGE("Failed to delete template %s", id.c_str());
        return 1;
    }
    return 0;
}

int profile_list_templates() {
    DIR* dir = opendir(PROFILE_TEMPLATE_DIR);
    if (!dir) {
        printf("No templates found\n");
        return 0;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.')
            continue;
        printf("%s\n", entry->d_name);
    }

    closedir(dir);
    return 0;
}

int apply_profile_sepolies() {
    DIR* dir = opendir(PROFILE_SELINUX_DIR);
    if (!dir)
        return 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.')
            continue;

        if (apply_profile_sepolicy(entry->d_name) == 0) {
            LOGI("Applied sepolicy for %s", entry->d_name);
        } else {
            LOGW("Failed to apply sepolicy for %s", entry->d_name);
        }
    }

    closedir(dir);
    return 0;
}

}  // namespace ksud

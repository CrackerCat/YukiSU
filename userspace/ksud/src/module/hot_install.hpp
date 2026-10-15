#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace ksud {

std::optional<std::string> take_hot_install_request(const std::filesystem::path& staged_dir);
bool apply_hot_install(const std::string& module_id, bool is_metamodule,
                       const std::string& run_script);
void clean_hot_install_trash();

}  // namespace ksud

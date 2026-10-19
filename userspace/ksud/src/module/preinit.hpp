#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ksud::preinit {

std::filesystem::path storage_dir();
const char* file_context(const std::filesystem::path& dir);
int sync(const std::filesystem::path& dir, const std::vector<std::filesystem::path>& stale,
         const std::map<std::string, std::filesystem::path>& modules, bool supported);
std::map<std::string, std::string> module_states();
void warn_status(const std::string& id);
void on_boot_completed();
int reset_bootcount();
int print_status(bool json = false);

}  // namespace ksud::preinit

#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ksud {

struct CliArguments;
int module_config_handle(const CliArguments& cli);

namespace module_config {
using Config = std::map<std::string, std::string>;
enum class Type { Persist, Temp };
constexpr size_t MAX_KEY_LEN = 256;
constexpr size_t MAX_VALUE_LEN = 1024 * 1024;
constexpr size_t MAX_COUNT = 32;

bool valid_id(std::string_view id);
bool valid_key(std::string_view key);
std::optional<Config> load(const std::string& module_id, Type type);
bool save(const std::string& module_id, Type type, const Config& config);
Config merge(const std::string& module_id);
}  // namespace module_config

// Clear all temporary configs (called during post-fs-data)
void clear_all_temp_configs();

}  // namespace ksud

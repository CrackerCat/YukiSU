#include "module_compat.hpp"
#include "../core/json.hpp"
#include "../core/ksucalls.hpp"
#include "../defs.hpp"
#include "../log.hpp"
#include "../terminal.hpp"
#include "../utils.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>

namespace ksud {
namespace {

constexpr const char* kConfigPath = "/data/adb/ksu/.module_compat.json";
constexpr const char* kCompatEnv = "YUKISU_MODULE_COMPAT";

struct ModuleCompat {
    bool enabled = false;
    uint32_t ksud_version_code = 0;
    uint32_t kernel_version_code = 0;
    uint32_t uapi_version = 0;
    std::string ksud_version;
};

bool valid_version_name(std::string_view value) {
    return value.size() < 255 &&
           std::none_of(
               value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

bool parse_code(std::string_view text, uint32_t& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && error == std::errc{} && end == text.data() + text.size();
}

bool parse_json_code(const json::Value& value, uint32_t& code) {
    if (!value.is_number() || !std::isfinite(value.n) || std::floor(value.n) != value.n ||
        value.n < 0 || value.n > std::numeric_limits<uint32_t>::max())
        return false;
    code = static_cast<uint32_t>(value.n);
    return true;
}

std::optional<ModuleCompat> parse_config(const std::string& text) {
    const auto value = json::parse(text);
    ModuleCompat config;
    if (!value.is_object() || !value.at("enabled").is_bool() ||
        !parse_json_code(value.at("ksud_version_code"), config.ksud_version_code) ||
        !parse_json_code(value.at("kernel_version_code"), config.kernel_version_code) ||
        !parse_json_code(value.at("uapi_version"), config.uapi_version) ||
        !value.at("ksud_version").is_string() || !valid_version_name(value.at("ksud_version").s))
        return std::nullopt;
    config.enabled = value.at("enabled").b;
    config.ksud_version = value.at("ksud_version").s;
    return config;
}

json::Value config_json(const ModuleCompat& config) {
    auto value = json::Value::object();
    value["enabled"] = config.enabled;
    value["ksud_version_code"] = static_cast<double>(config.ksud_version_code);
    value["kernel_version_code"] = static_cast<double>(config.kernel_version_code);
    value["uapi_version"] = static_cast<double>(config.uapi_version);
    value["ksud_version"] = config.ksud_version;
    return value;
}

std::optional<ModuleCompat> load_config() {
    std::error_code error;
    if (!std::filesystem::exists(kConfigPath, error) && !error)
        return ModuleCompat{};
    const auto content = read_file(kConfigPath);
    return !error && content ? parse_config(*content) : std::nullopt;
}

ModuleVersions resolve_versions(const ModuleCompat& config, bool include_kernel = true) {
    ModuleVersions versions{VERSION_NAME,
                            VERSION_CODE,
                            include_kernel ? std::to_string(get_version()) : "",
                            std::to_string(uapi_version()),
                            include_kernel ? std::to_string(get_uapi_version()) : "",
                            ""};
    if (config.enabled) {
        if (config.ksud_version_code)
            versions.ksud_version_code = std::to_string(config.ksud_version_code);
        if (config.kernel_version_code)
            versions.kernel_version_code = std::to_string(config.kernel_version_code);
        if (config.uapi_version) {
            versions.uapi_version = std::to_string(config.uapi_version);
            versions.kernel_uapi_version = versions.uapi_version;
        }
        if (!config.ksud_version.empty())
            versions.ksud_version = config.ksud_version;
        versions.compat_environment = json::dump(config_json(config));
    }
    return versions;
}

std::string shell_quote(std::string_view value) {
    std::string result = "'";
    for (const char c : value) {
        if (c == '\'')
            result += "'\\''";
        else
            result += c;
    }
    return result + '\'';
}

}  // namespace

ModuleVersions module_script_versions() {
    const auto config = load_config();
    if (!config)
        LOGW("Invalid module compatibility configuration; using actual versions");
    return resolve_versions(config.value_or(ModuleCompat{}));
}

ModuleVersions module_cli_versions(bool include_kernel) {
    const char* inherited = getenv(kCompatEnv);
    const auto config = inherited ? parse_config(inherited) : std::nullopt;
    return resolve_versions(config.value_or(ModuleCompat{}), include_kernel);
}

void apply_module_version_env(const ModuleVersions& versions) {
    setenv("KSU_VER", versions.ksud_version.c_str(), 1);
    setenv("KSU_VER_CODE", versions.ksud_version_code.c_str(), 1);
    setenv("KSU_KERNEL_VER_CODE", versions.kernel_version_code.c_str(), 1);
    setenv("KSU_UAPI_VER", versions.uapi_version.c_str(), 1);
    if (versions.compat_environment.empty())
        unsetenv(kCompatEnv);
    else
        setenv(kCompatEnv, versions.compat_environment.c_str(), 1);
}

int module_compat_handle(const std::vector<std::string>& args) {
    if (args.size() == 1 && args[0] == "get") {
        const auto config = load_config();
        if (!config)
            return terminal::error("Invalid or unreadable module compatibility configuration");
        auto result = config_json(*config);
        const auto actual = resolve_versions({});
        auto values = json::Value::object();
        values["ksud_version"] = actual.ksud_version;
        values["ksud_version_code"] = actual.ksud_version_code;
        values["kernel_version_code"] = actual.kernel_version_code;
        values["uapi_version"] = actual.uapi_version;
        result["actual"] = values;
        printf("%s\n", json::dump(result).c_str());
        return 0;
    }
    if (args.size() == 1 && args[0] == "env") {
        const auto versions = module_script_versions();
        printf("export KSU_VER=%s KSU_VER_CODE=%s KSU_KERNEL_VER_CODE=%s KSU_UAPI_VER=%s\n",
               shell_quote(versions.ksud_version).c_str(),
               shell_quote(versions.ksud_version_code).c_str(),
               shell_quote(versions.kernel_version_code).c_str(),
               shell_quote(versions.uapi_version).c_str());
        if (versions.compat_environment.empty())
            printf("unset %s\n", kCompatEnv);
        else
            printf("export %s=%s\n", kCompatEnv, shell_quote(versions.compat_environment).c_str());
        return 0;
    }
    if (args.empty() || (args[0] != "set" && args[0] != "reset"))
        return terminal::error("Expected module compat get, set, reset, or env");
    ModuleCompat config;
    if (args[0] == "reset" && args.size() != 1)
        return terminal::error("Unexpected reset arguments");
    for (size_t i = 1; i < args.size(); i += 2) {
        if (i + 1 == args.size())
            return terminal::error("Missing compatibility option value");
        const auto& option = args[i];
        const auto& value = args[i + 1];
        if (option == "--enabled" && (value == "true" || value == "false"))
            config.enabled = value == "true";
        else if (option == "--ksud-version-code" || option == "--kernel-version-code") {
            auto& code = option == "--ksud-version-code" ? config.ksud_version_code
                                                         : config.kernel_version_code;
            if (!parse_code(std::string_view(value), code))
                return terminal::error("Invalid version code: " + value);
        } else if (option == "--uapi") {
            if (!parse_code(std::string_view(value), config.uapi_version))
                return terminal::error("Invalid UAPI version: " + value);
        } else if (option == "--ksud-version" && valid_version_name(value))
            config.ksud_version = value;
        else
            return terminal::error("Invalid module compatibility option: " + option);
    }
    if (!ensure_dir_exists(WORKING_DIR) ||
        !write_file_atomic(kConfigPath, json::dump(config_json(config)) + '\n'))
        return terminal::error("Failed to save module compatibility configuration");
    return 0;
}

}  // namespace ksud

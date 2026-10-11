#pragma once

#include <string>
#include <vector>

namespace ksud {

struct ModuleVersions {
    std::string ksud_version;
    std::string ksud_version_code;
    std::string kernel_version_code;
    std::string uapi_version;
    std::string kernel_uapi_version;
    std::string compat_environment;
};

ModuleVersions module_script_versions();
ModuleVersions module_cli_versions(bool include_kernel = false);
void apply_module_version_env(const ModuleVersions& versions);
int module_compat_handle(const std::vector<std::string>& args);

}  // namespace ksud

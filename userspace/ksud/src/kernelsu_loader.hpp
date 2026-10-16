#pragma once

#include <string>

namespace ksud::kernelsu_loader {

bool load_module(const char* path, const std::string& param_values = "",
                 bool kernel_lookup = false);

}  // namespace ksud::kernelsu_loader

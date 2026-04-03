#pragma once

#include <string>

namespace ksud::late_load {

int run(bool post_magica = false, bool allow_shell = false, const std::string& kmi = {});

}  // namespace ksud::late_load

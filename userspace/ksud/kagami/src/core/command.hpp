#pragma once

#include "core/command_capture.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kagami {

struct CommandResult {
    int exit_code = 0;
    int error_number = 0;
    std::string_view stdout_text;
    std::string_view stderr_text;
};

int run_command(const std::vector<std::string>& args);
CommandResult run_command_capture(const std::vector<std::string>& args, CommandCapture& capture);

}  // namespace kagami

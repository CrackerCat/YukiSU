#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ksud {

struct KsuConfigArgs {
    bool allow_shell = false;
    bool no_custom_rc = false;
    std::vector<std::string> forced_features;

    bool add_forced_feature(std::string_view input) {
        constexpr std::array<std::string_view, 5> names = {"su_compat", "kernel_umount", "sulog",
                                                           "adb_root", "selinux_hide"};
        const auto separator = input.find('=');
        if (separator == std::string_view::npos)
            return false;
        const auto name = input.substr(0, separator);
        auto number = input.substr(separator + 1);
        std::size_t id = 0;
        for (; id < names.size(); ++id) {
            if (name == names[id] || name == std::to_string(id))
                break;
        }
        if (id == names.size() || number.empty())
            return false;
        int base = 10;
        if (number.size() > 2 && number[0] == '0' && (number[1] == 'x' || number[1] == 'X')) {
            number.remove_prefix(2);
            base = 16;
        }
        std::uint64_t value = 0;
        const auto parsed =
            std::from_chars(number.data(), number.data() + number.size(), value, base);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size())
            return false;
        forced_features.push_back(std::string(names[id]) + ":" + std::to_string(value));
        return true;
    }

    std::string module_params() const {
        std::string result;
        if (allow_shell)
            result = "allow_shell=1";
        if (no_custom_rc)
            result += (result.empty() ? "" : " ") + std::string("norc=1");
        if (!forced_features.empty()) {
            if (!result.empty())
                result += ' ';
            result += "force_feature=";
            for (const auto& entry : forced_features) {
                if (result.back() != '=')
                    result += ',';
                result += entry;
            }
        }
        return result;
    }
};

}  // namespace ksud

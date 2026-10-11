#pragma once

#include <cstdint>
#include <string>

namespace ksud {

struct ApkSignatureInfo {
    struct V2Info {
        bool has = false;
        uint32_t size = 0;
        std::string hash;
    };

    bool valid = false;
    bool v1 = false;
    V2Info v2;
    bool v3 = false;
    bool v31 = false;
    std::string manager_error = "invalid_apk";
    std::string manager_error_detail;
};

ApkSignatureInfo get_apk_signature(const std::string& apk_path);
std::string manager_apk_error_message(const ApkSignatureInfo& info);

}  // namespace ksud

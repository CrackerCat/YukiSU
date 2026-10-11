#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>

struct zip;

namespace ksud {

struct ZipLimits {
    uint64_t package_size = std::numeric_limits<uint64_t>::max();
    uint64_t entries = 32768;
    uint64_t directory_size = 16U * 1024U * 1024U;
};

struct ZipEntry {
    std::string name;
    uint64_t size = 0;
    uint32_t attributes = 0;
    uint16_t method = 0;
    bool directory = false;
    bool symlink = false;
    bool encrypted = false;
    bool supported = false;
};

class ZipReader {
public:
    explicit ZipReader(const std::filesystem::path& path, ZipLimits limits = {});
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
    ZipReader(ZipReader&&) = delete;
    ZipReader& operator=(ZipReader&&) = delete;

    [[nodiscard]] bool valid() const { return archive_ != nullptr; }
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] uint64_t entry_count() const;
    bool entry(uint64_t index, ZipEntry& result);
    std::optional<std::string> read_text(uint64_t index, uint64_t limit);
    bool extract_to_fd(uint64_t index, int fd, uint64_t limit);

private:
    bool read_entry(uint64_t index, uint64_t limit, std::string* text, int fd);
    zip* archive_ = nullptr;
    std::string error_;
};

}  // namespace ksud

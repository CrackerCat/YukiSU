#include "archive_zip.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zip.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <memory>
#include <vector>

namespace ksud {
namespace {

uint64_t little_endian(const unsigned char* data, size_t size) {
    uint64_t result = 0;
    for (size_t i = 0; i < size; ++i)
        result |= uint64_t{data[i]} << (8U * i);
    return result;
}

bool read_at(int fd, unsigned char* data, size_t size, uint64_t offset) {
    size_t done = 0;
    while (done < size) {
        const ssize_t count =
            pread(fd, data + done, size - done, static_cast<off_t>(offset + done));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

// Bound directory allocations before libzip opens an untrusted archive.
bool check_directory(int fd, uint64_t file_size, const ZipLimits& limits) {
    if (file_size < 22)
        return false;
    std::vector<unsigned char> tail(static_cast<size_t>(std::min<uint64_t>(file_size, 65557)));
    const uint64_t tail_offset = file_size - tail.size();
    if (!read_at(fd, tail.data(), tail.size(), tail_offset))
        return false;
    for (size_t pos = tail.size() - 22;; --pos) {
        const auto* end = tail.data() + pos;
        if (little_endian(end, 4) == 0x06054b50 &&
            little_endian(end + 20, 2) == tail.size() - pos - 22) {
            if (little_endian(end + 4, 2) != 0 || little_endian(end + 6, 2) != 0)
                return false;
            uint64_t count = little_endian(end + 10, 2);
            uint64_t bytes = little_endian(end + 12, 4);
            uint64_t offset = little_endian(end + 16, 4);
            uint64_t directory_end = tail_offset + pos;
            if (count == 0xffff || bytes == 0xffffffff || offset == 0xffffffff) {
                std::array<unsigned char, 20> locator{};
                std::array<unsigned char, 56> zip64{};
                if (directory_end < locator.size() ||
                    !read_at(fd, locator.data(), locator.size(), directory_end - locator.size()) ||
                    little_endian(locator.data(), 4) != 0x07064b50 ||
                    little_endian(locator.data() + 4, 4) != 0 ||
                    little_endian(locator.data() + 16, 4) != 1)
                    return false;
                const uint64_t zip64_offset = little_endian(locator.data() + 8, 8);
                if (zip64_offset > directory_end - locator.size() ||
                    directory_end - locator.size() - zip64_offset < zip64.size() ||
                    !read_at(fd, zip64.data(), zip64.size(), zip64_offset) ||
                    little_endian(zip64.data(), 4) != 0x06064b50 ||
                    little_endian(zip64.data() + 4, 8) < 44 ||
                    little_endian(zip64.data() + 16, 4) != 0 ||
                    little_endian(zip64.data() + 20, 4) != 0)
                    return false;
                count = little_endian(zip64.data() + 32, 8);
                if (little_endian(zip64.data() + 24, 8) != count)
                    return false;
                bytes = little_endian(zip64.data() + 40, 8);
                offset = little_endian(zip64.data() + 48, 8);
                directory_end = zip64_offset;
            } else if (little_endian(end + 8, 2) != count) {
                return false;
            }
            return count <= limits.entries && bytes <= limits.directory_size &&
                   offset <= directory_end && bytes <= directory_end - offset;
        }
        if (pos == 0)
            break;
    }
    return false;
}

bool supported_method(uint16_t method) {
    switch (method) {
    case ZIP_CM_STORE:
    case ZIP_CM_DEFLATE:
    case ZIP_CM_BZIP2:
    case ZIP_CM_LZMA:
    case ZIP_CM_XZ:
        return zip_compression_method_supported(method, 0) != 0;
    default:
        return false;
    }
}

class ScopedFd {
public:
    explicit ScopedFd(int fd) : fd_(fd) {}
    ~ScopedFd() { reset(-1); }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    ScopedFd(ScopedFd&&) = delete;
    ScopedFd& operator=(ScopedFd&&) = delete;
    [[nodiscard]] int get() const { return fd_; }
    void reset(int fd) {
        if (fd_ >= 0)
            close(fd_);
        fd_ = fd;
    }

private:
    int fd_;
};

struct EntryCloser {
    void operator()(zip_file_t* file) const { zip_fclose(file); }
};

}  // namespace

ZipReader::ZipReader(const std::filesystem::path& path, ZipLimits limits) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        error_ = "Cannot open archive: " + std::string(strerror(errno));
        return;
    }
    struct stat status{};
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size <= 0 ||
        static_cast<uint64_t>(status.st_size) > limits.package_size ||
        !check_directory(fd, static_cast<uint64_t>(status.st_size), limits)) {
        close(fd);
        error_ = "Invalid archive or ZIP directory exceeds limits";
        return;
    }
    int code = 0;
    // Read local metadata lazily; checking every local extra field here defeats the directory
    // budget.
    archive_ = zip_fdopen(fd, 0, &code);
    if (archive_ == nullptr) {
        close(fd);
        zip_error_t error;
        zip_error_init_with_code(&error, code);
        error_ = zip_error_strerror(&error);
        zip_error_fini(&error);
        return;
    }
    if (entry_count() > limits.entries) {
        zip_discard(archive_);
        archive_ = nullptr;
        error_ = "ZIP entry count exceeds limits";
    }
}

ZipReader::~ZipReader() {
    if (archive_ != nullptr)
        zip_discard(archive_);
}

uint64_t ZipReader::entry_count() const {
    return archive_ == nullptr ? 0 : static_cast<uint64_t>(zip_get_num_entries(archive_, 0));
}

bool ZipReader::entry(uint64_t index, ZipEntry& result) {
    zip_stat_t status{};
    uint8_t system = 0;
    uint32_t attributes = 0;
    if (archive_ == nullptr)
        return false;
    if (zip_stat_index(archive_, index, 0, &status) != 0 ||
        zip_file_get_external_attributes(archive_, index, 0, &system, &attributes) != 0) {
        error_ = zip_strerror(archive_);
        return false;
    }
    constexpr uint64_t required =
        ZIP_STAT_NAME | ZIP_STAT_SIZE | ZIP_STAT_COMP_METHOD | ZIP_STAT_ENCRYPTION_METHOD;
    if ((status.valid & required) != required || status.name == nullptr || status.name[0] == '\0') {
        error_ = "ZIP entry has incomplete metadata";
        return false;
    }
    result.name = status.name;
    result.size = status.size;
    result.attributes = attributes;
    result.method = status.comp_method;
    result.directory = result.name.back() == '/';
    result.symlink = system == ZIP_OPSYS_UNIX && ((attributes >> 16U) & S_IFMT) == S_IFLNK;
    result.encrypted = status.encryption_method != ZIP_EM_NONE;
    result.supported = supported_method(result.method);
    return true;
}

bool ZipReader::read_entry(uint64_t index, uint64_t limit, std::string* text, int fd,
                           bool symlink) {
    ZipEntry metadata;
    if (!entry(index, metadata))
        return false;
    if (metadata.directory || metadata.symlink != symlink || metadata.encrypted ||
        !metadata.supported || metadata.size > limit) {
        error_ = "ZIP entry is unsupported or exceeds the size limit";
        return false;
    }
    std::unique_ptr<zip_file_t, EntryCloser> file(zip_fopen_index(archive_, index, 0));
    if (!file) {
        error_ = zip_strerror(archive_);
        return false;
    }
    std::array<char, 65536> buffer{};
    uint64_t total = 0;
    for (;;) {
        const zip_int64_t count = zip_fread(file.get(), buffer.data(), buffer.size());
        if (count < 0) {
            error_ = zip_file_strerror(file.get());
            return false;
        }
        if (count == 0)
            break;
        const auto size = static_cast<size_t>(count);
        if (total > metadata.size || size > metadata.size - total) {
            error_ = "ZIP entry exceeds its declared size";
            return false;
        }
        if (text != nullptr) {
            text->append(buffer.data(), size);
        } else {
            size_t written = 0;
            while (written < size) {
                const ssize_t bytes = write(fd, buffer.data() + written, size - written);
                if (bytes < 0 && errno == EINTR)
                    continue;
                if (bytes <= 0) {
                    error_ = "Cannot write archive entry: " + std::string(strerror(errno));
                    return false;
                }
                written += static_cast<size_t>(bytes);
            }
        }
        total += size;
    }
    if (total != metadata.size) {
        error_ = "ZIP entry length mismatch";
        return false;
    }
    if (zip_fclose(file.release()) != 0) {
        error_ = "Cannot finalize ZIP entry";
        return false;
    }
    return true;
}

std::optional<std::string> ZipReader::read_text(uint64_t index, uint64_t limit) {
    std::string text;
    if (!read_entry(index, limit, &text, -1))
        return std::nullopt;
    return text;
}

bool ZipReader::extract_to_fd(uint64_t index, int fd, uint64_t limit) {
    return read_entry(index, limit, nullptr, fd);
}

bool ZipReader::extract_to_directory(const std::filesystem::path& destination,
                                     std::string_view excluded) {
    const ScopedFd root(open(destination.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!valid() || root.get() < 0) {
        error_ = "Cannot open extraction directory";
        return false;
    }
    for (uint64_t index = 0; index < entry_count(); ++index) {
        ZipEntry metadata;
        if (!entry(index, metadata))
            return false;
        std::replace(metadata.name.begin(), metadata.name.end(), '\\', '/');
        if (metadata.name.front() == '/' ||
            (metadata.name.size() >= 2 && metadata.name[1] == ':')) {
            error_ = "Absolute ZIP entry path";
            return false;
        }
        std::vector<std::string> parts;
        for (const auto& part : std::filesystem::path(metadata.name)) {
            if (part == "..") {
                error_ = "Parent traversal in ZIP entry path";
                return false;
            }
            if (!part.empty() && part != ".")
                parts.push_back(part.string());
        }
        if (parts.empty()) {
            if (metadata.directory)
                continue;
            error_ = "Empty ZIP entry path";
            return false;
        }
        if (!excluded.empty() && parts.front() == excluded)
            continue;
        if (metadata.encrypted || !metadata.supported) {
            error_ = "Unsupported ZIP entry";
            return false;
        }
        ScopedFd directory(fcntl(root.get(), F_DUPFD_CLOEXEC, 0));
        if (directory.get() < 0) {
            error_ = "Cannot duplicate extraction directory: " + std::string(strerror(errno));
            return false;
        }
        const size_t directories = parts.size() - (metadata.directory ? 0U : 1U);
        for (size_t part = 0; part < directories; ++part) {
            if (mkdirat(directory.get(), parts[part].c_str(), 0755) != 0 && errno != EEXIST) {
                error_ = "Cannot create ZIP directory: " + std::string(strerror(errno));
                return false;
            }
            // Never follow a previously extracted symlink while writing later entries.
            directory.reset(openat(directory.get(), parts[part].c_str(),
                                   O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
            if (directory.get() < 0) {
                error_ = "Unsafe ZIP directory: " + std::string(strerror(errno));
                return false;
            }
        }
        if (metadata.directory)
            continue;
        if (metadata.symlink) {
            std::string target;
            if (!read_entry(index, PATH_MAX - 1, &target, -1, true))
                return false;
            if (target.empty() || target.find('\0') != std::string::npos ||
                symlinkat(target.c_str(), directory.get(), parts.back().c_str()) != 0) {
                error_ = "Cannot create ZIP symlink";
                return false;
            }
            continue;
        }
        const ScopedFd output(openat(directory.get(), parts.back().c_str(),
                                     O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
        if (output.get() < 0) {
            error_ = "Cannot create ZIP file: " + std::string(strerror(errno));
            return false;
        }
        if (!extract_to_fd(index, output.get(), metadata.size))
            return false;
        mode_t mode = static_cast<mode_t>(metadata.attributes >> 16U) & 0777;
        if (mode == 0)
            mode = 0644;
        if (fchmod(output.get(), mode) != 0) {
            error_ = "Cannot set ZIP file permissions: " + std::string(strerror(errno));
            return false;
        }
    }
    return true;
}

}  // namespace ksud

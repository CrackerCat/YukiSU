#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <streambuf>
#include <string_view>

namespace kagami {

class CaptureBuffer final : public std::streambuf {
public:
    bool prepare() {
        storage_.reset(new (std::nothrow) char[capacity]);
        if (!storage_)
            return false;
        reset();
        return true;
    }
    void reset() {
        setp(storage_.get(), storage_.get() + capacity);
        failed_ = false;
    }
    bool failed() const { return failed_; }
    std::string_view view() const { return {pbase(), static_cast<std::size_t>(pptr() - pbase())}; }

protected:
    int_type overflow(int_type value) override {
        if (traits_type::eq_int_type(value, traits_type::eof()))
            return traits_type::not_eof(value);
        failed_ = true;
        return traits_type::eof();
    }
    std::streamsize xsputn(const char* value, std::streamsize count) override {
        if (count <= 0)
            return 0;
        const auto size = std::min<std::streamsize>(count, epptr() - pptr());
        std::memcpy(pptr(), value, static_cast<std::size_t>(size));
        pbump(static_cast<int>(size));
        failed_ |= size != count;
        return size;
    }

private:
    static constexpr std::size_t capacity = 1024UL * 1024;
    std::unique_ptr<char[]> storage_;
    bool failed_ = false;
};

// Returned command views remain valid until this storage is reused.
struct CommandCapture {
    CaptureBuffer out, err;
    bool prepare() { return out.prepare() && err.prepare(); }
};

}  // namespace kagami

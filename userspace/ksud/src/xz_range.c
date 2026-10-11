#include "xz_range.hpp"

#include <errno.h>
#include <lzma.h>
#include <unistd.h>

static int write_all(int fd, const uint8_t* data, size_t size) {
    while (size != 0) {
        ssize_t count = write(fd, data, size);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        data += count;
        size -= (size_t)count;
    }
    return 0;
}

int yukisu_unpack_xz_range_to_fd(const uint8_t* packed, size_t packed_size, uint64_t range_offset,
                                 size_t range_size, uint64_t unpacked_size, int dst_fd) {
    if (!packed || packed_size == 0 || dst_fd < 0 || range_offset > unpacked_size ||
        range_size > unpacked_size - range_offset) {
        errno = EINVAL;
        return -1;
    }

    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_stream_decoder(&stream, UINT64_C(128) * 1024 * 1024, LZMA_CONCATENATED) != LZMA_OK)
        return -1;
    stream.next_in = packed;
    stream.avail_in = packed_size;
    uint8_t buffer[65536];
    uint64_t offset = 0;
    uint64_t written = 0;
    const uint64_t range_end = range_offset + range_size;
    int result = -1;

    for (;;) {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        const size_t previous_input = stream.avail_in;
        const lzma_ret status = lzma_code(&stream, LZMA_FINISH);
        const size_t produced = sizeof(buffer) - stream.avail_out;
        if ((status != LZMA_OK && status != LZMA_STREAM_END) || offset > unpacked_size ||
            produced > unpacked_size - offset)
            break;
        const uint64_t end = offset + produced;
        if (offset < range_end && end > range_offset) {
            const uint64_t first = offset > range_offset ? offset : range_offset;
            const uint64_t last = end < range_end ? end : range_end;
            const size_t size = (size_t)(last - first);
            if (write_all(dst_fd, buffer + (size_t)(first - offset), size) != 0)
                break;
            written += size;
        }
        offset = end;
        if (status == LZMA_STREAM_END) {
            if (stream.avail_in == 0 && offset == unpacked_size && written == range_size)
                result = 0;
            break;
        }
        if (produced == 0 && stream.avail_in == previous_input)
            break;
    }
    lzma_end(&stream);
    return result;
}

#pragma once
#include <cstdint>
#include <cstring>
#include <climits>
#include <algorithm>

namespace geoputer {
// Caller owns the medium exclusively and serializes callbacks. Validate the
// complete range before any writes; preserve neighboring bytes on partial I/O.
template <typename Read, typename Write>
int32_t transferUsbBlocks(uint32_t sectors, uint32_t lba, uint32_t offset, void* buffer, uint32_t size,
                         bool writing, Read read, Write write, uint8_t (&scratch)[512]) {
    const uint64_t start = static_cast<uint64_t>(lba) * 512 + offset;
    const uint64_t capacity = static_cast<uint64_t>(sectors) * 512;
    if (size > INT32_MAX || start > capacity || size > capacity - start || (!buffer && size)) return -1;
    auto* data = static_cast<uint8_t*>(buffer);
    uint64_t position = start;
    uint32_t done = 0;
    while (done < size) {
        const auto sector = static_cast<uint32_t>(position / 512);
        const auto within = static_cast<uint32_t>(position % 512);
        // Pass aligned runs straight from TinyUSB's buffer to the SD driver.
        // Its multi-block commands avoid a command/transaction per sector.
        if (!within && size - done >= 512) {
            const auto blocks = (size - done) / 512;
            if (writing ? !write(data + done, sector, blocks) : !read(data + done, sector, blocks)) return -1;
            const auto bytes = blocks * 512;
            done += bytes; position += bytes;
            continue;
        }
        const auto count = std::min<uint32_t>(512 - within, size - done);
        if (!read(scratch, sector, 1)) return -1;
        if (writing) {
            std::memcpy(scratch + within, data + done, count);
            if (!write(scratch, sector, 1)) return -1;
        } else {
            std::memcpy(data + done, scratch + within, count);
        }
        done += count; position += count;
    }
    return static_cast<int32_t>(done);
}
}

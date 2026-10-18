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
        const auto count = std::min<uint32_t>(512 - within, size - done);
        if (!within && count == 512) {
            if (writing ? !write(data + done, sector) : !read(data + done, sector)) return -1;
        } else {
            if (!read(scratch, sector)) return -1;
            if (writing) {
                std::memcpy(scratch + within, data + done, count);
                if (!write(scratch, sector)) return -1;
            } else std::memcpy(data + done, scratch + within, count);
        }
        done += count; position += count;
    }
    return static_cast<int32_t>(done);
}
}

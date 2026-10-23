#include "DiskIndexedMapReader.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace geoputer {
namespace {
uint32_t get(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void check(bool value) {
    if (!value) throw std::runtime_error("Map index changed or invalid; revalidate dataset");
}
struct Destination {
    int width, height, dx, dy, pitch;
    uint32_t* pixels;
    void set(int x, int y, int64_t value) {
        if (value < 0 || uint64_t(value) > UINT32_MAX)
            throw std::runtime_error("region code exceeds uint32 cache range");
        auto& pixel = pixels[size_t(y + dy) * pitch + x + dx];
        if (value && !pixel) pixel = static_cast<uint32_t>(value); // First nonzero chunk wins.
    }
};

// Find the first requested block at or after the group's beginning. This avoids
// reading groups that fall in the gaps between requested rows of a rectangle.
bool intersectsBlocks(uint32_t first, uint32_t count, uint64_t columns,
                      uint64_t x0, uint64_t x1, uint64_t y0, uint64_t y1) {
    uint64_t row = std::max(y0, first / columns);
    uint64_t column = row == first / columns ? std::max(x0, first % columns) : x0;
    if (column > x1) { ++row; column = x0; }
    return row <= y1 && row * columns + column < uint64_t(first) + count;
}
} // namespace

uint32_t DiskIndexedMapReader::value(size_t offset) const { return get(info_.header.data() + offset); }

void DiskIndexedMapReader::readRect(const galuchat::Rect& target, uint32_t* pixels) const {
    check(target.width > 0 && target.height > 0 && pixels);
    std::fill_n(pixels, size_t(target.width) * target.height, 0);
    const uint64_t map_end = uint64_t(64) + value(60);
    check(map_end <= SIZE_MAX);
    auto index = index_->createRange(0, static_cast<size_t>(map_end));
    std::array<uint8_t, 64> header{};
    for (auto& b : header) b = index->readByte();
    check(header == info_.header);
    uint64_t previous_end = 0;
    for (uint32_t i = 0; i < chunkCount(); ++i) {
        check(uint64_t(index->pos()) + 40 <= map_end);
        std::array<uint8_t, 40> record{};
        for (auto& b : record) b = index->readByte();
        const auto* p = record.data();
        const int64_t west = static_cast<int32_t>(get(p)), south = static_cast<int32_t>(get(p + 4));
        const uint32_t width = get(p + 8), height = get(p + 12), square = get(p + 16);
        const uint32_t start = get(p + 20), bytes = get(p + 24), first = get(p + 28);
        const uint32_t blocks = get(p + 32), groups = get(p + 36);
        const uint64_t end = uint64_t(start) + bytes;
        check(width > 0 && height > 0 && width <= INT32_MAX && height <= INT32_MAX
              && square && square <= uint32_t(1) << 30 && !(square & (square - 1)));
        check(west + width <= INT32_MAX && south + height <= INT32_MAX && bytes
              && end <= value(20) && first > start && first < end);
        check(start >= previous_end && (!i || start == previous_end));
        const uint64_t columns = (uint64_t(width) + square - 1) / square;
        check(uint64_t(blocks) == columns * ((uint64_t(height) + square - 1) / square));
        check(bytes < config::IndexMinChunkBytes ? groups == 0 : groups > 0 && groups <= blocks);
        const uint64_t group_bytes = uint64_t(groups) * 16;
        check(uint64_t(index->pos()) + group_bytes <= map_end && group_bytes <= SIZE_MAX);
        const auto left = std::max<int64_t>(target.x, west), bottom = std::max<int64_t>(target.y, south);
        const auto right = std::min<int64_t>(int64_t(target.x) + target.width, west + width);
        const auto top = std::min<int64_t>(int64_t(target.y) + target.height, south + height);
        if (left >= right || bottom >= top) {
            index->skipInByte(static_cast<size_t>(group_bytes));
            previous_end = end;
            continue;
        }
        Destination view{int(right - left), int(top - bottom), int(left - target.x),
                         int(bottom - target.y), target.width, pixels};
        const int x = int(left - west), y = int(bottom - south);
        if (!groups) {
            auto stream = map_->createRange(start, bytes);
            galuchat::GaluchatImageDataChunk01Reader chunk(*stream);
            check(chunk.width == int(width) && chunk.height == int(height)
                  && chunk.square_unit == int(square) && uint64_t(start) + stream->pos() == first);
            chunk.readRectWithoutTail(x, y, view);
        } else {
            const uint64_t x0 = uint64_t(x) / square, x1 = uint64_t(right - west - 1) / square;
            const uint64_t y0 = uint64_t(y) / square, y1 = uint64_t(top - south - 1) / square;
            uint64_t next_block = 0, next_offset = first;
            std::unique_ptr<galuchat::ByteReader> stream;
            uint64_t stream_base = 0;
            for (uint32_t g = 0; g < groups; ++g) {
                std::array<uint8_t, 16> entry{};
                for (auto& b : entry) b = index->readByte();
                const uint32_t number = get(entry.data()), count = get(entry.data() + 4);
                const uint32_t offset = get(entry.data() + 8), length = get(entry.data() + 12);
                check(number == next_block && count && uint64_t(number) + count <= blocks
                      && offset == next_offset && length && uint64_t(offset) + length <= end);
                next_block += count;
                next_offset += length;
                if (!intersectsBlocks(number, count, columns, x0, x1, y0, y1)) continue;
                if (!stream) {
                    stream = map_->createRange(offset, static_cast<size_t>(end - offset));
                    stream_base = offset;
                } else {
                    const uint64_t position = stream_base + stream->pos();
                    check(position <= offset);
                    stream->skipInByte(static_cast<size_t>(offset - position));
                }
                // The range API bounds decoding to this group. Reuse the same
                // file/buffer for later groups instead of reopening the GLC.
                galuchat::GaluchatImageDataChunk01Reader::readRectFromBlockRange(
                    *stream, {int(width), int(height), int(square), number, count, length}, x, y, view);
            }
            check(next_block == blocks && next_offset == end);
        }
        previous_end = end;
    }
    check(index->pos() == map_end && previous_end == value(20));
}
} // namespace geoputer

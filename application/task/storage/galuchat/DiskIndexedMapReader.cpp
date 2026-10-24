#include "DiskIndexedMapReader.h"
#include "../dataset/DatasetIndexFormat.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace geoputer {
namespace {
using namespace index_format;

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
    // Coordinates are clipped by the decoder; preserve first-nonzero composition.
    void setSpan(int x, int y, int count, int step_x, int step_y, int64_t value) {
        if (value < 0 || uint64_t(value) > UINT32_MAX)
            throw std::runtime_error("region code exceeds uint32 cache range");
        if (!value) return;
        const auto code = static_cast<uint32_t>(value);
        auto* pixel = pixels + size_t(y + dy) * pitch + x + dx;
        const std::ptrdiff_t stride = std::ptrdiff_t(step_y) * pitch + step_x;
        while (count > 0) {
            if (!*pixel) *pixel = code;
            // Do not form an out-of-array pointer after the last reverse write.
            if (--count) pixel += stride;
        }
    }
};

// Logical dimensions still describe the full-resolution intersection. Only the
// chosen sample lattice is stored; never fold multiple source codes into one pixel.
struct DecimatedDestination {
    int width, height, dx, dy, pitch;
    unsigned scale;
    uint32_t* pixels;
    static void validate(int64_t value) {
        if (value < 0 || uint64_t(value) > UINT32_MAX)
            throw std::runtime_error("region code exceeds uint32 cache range");
    }
    void set(int x, int y, int64_t value) {
        validate(value);
        x += dx; y += dy;
        if (unsigned(x) % scale || unsigned(y) % scale || !value) return;
        auto& pixel = pixels[size_t(unsigned(y) / scale) * pitch + unsigned(x) / scale];
        if (!pixel) pixel = static_cast<uint32_t>(value);
    }
    void setSpan(int x, int y, int count, int step_x, int step_y, int64_t value) {
        validate(value);
        if (!value || count <= 0) return;
        x += dx; y += dy;
        const unsigned along = unsigned(step_x ? x : y);
        const unsigned across = unsigned(step_x ? y : x);
        if (across % scale) return;
        const int direction = step_x ? step_x : step_y;
        const unsigned remainder = along % scale;
        const unsigned skip = direction > 0 ? (scale - remainder) % scale : remainder;
        if (skip >= unsigned(count)) return;
        x += int(skip) * step_x; y += int(skip) * step_y;
        auto* pixel = pixels + size_t(unsigned(y) / scale) * pitch + unsigned(x) / scale;
        const std::ptrdiff_t stride = std::ptrdiff_t(step_y) * pitch + step_x;
        unsigned selected = 1 + (unsigned(count) - skip - 1) / scale;
        while (selected) {
            if (!*pixel) *pixel = static_cast<uint32_t>(value);
            if (--selected) pixel += stride;
        }
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
template<class MakeDestination>
void readRectangle(const DatasetMapIndex& info, const galuchat::ReaderFactory& map,
                   const galuchat::ReaderFactory& index_factory, const galuchat::Rect& target,
                   MakeDestination make_destination) {
    auto value = [&](size_t offset) { return index_format::get(info.header.data() + offset); };
    const uint64_t map_end = uint64_t(64) + value(index_format::Header::MapIndexBytes);
    check(map_end <= SIZE_MAX);
    auto index = index_factory.createRange(0, static_cast<size_t>(map_end));
    const auto header = readRecord<Header::Bytes>(*index);
    check(header == info.header);
    uint64_t previous_end = 0;
    for (uint32_t i = 0; i < value(Header::ChunkCount); ++i) {
        check(uint64_t(index->pos()) + 40 <= map_end);
        const auto record = readRecord<ChunkRecord::Bytes>(*index);
        const auto* p = record.data();
        const int64_t west = static_cast<int32_t>(get(p)), south = static_cast<int32_t>(get(p + ChunkRecord::South));
        const uint32_t width = get(p + ChunkRecord::Width), height = get(p + ChunkRecord::Height), square = get(p + ChunkRecord::SquareUnit);
        const uint32_t start = get(p + ChunkRecord::ChunkOffset), bytes = get(p + ChunkRecord::ChunkBytes), first = get(p + ChunkRecord::FirstBlockOffset);
        const uint32_t blocks = get(p + ChunkRecord::BlockCount), groups = get(p + ChunkRecord::GroupCount);
        const uint64_t end = uint64_t(start) + bytes;
        check(width > 0 && height > 0 && width <= INT32_MAX && height <= INT32_MAX
              && square && square <= uint32_t(1) << 30 && !(square & (square - 1)));
        check(west + width <= INT32_MAX && south + height <= INT32_MAX && bytes
              && end <= value(index_format::Header::MapBytes) && first > start && first < end);
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
        auto view = make_destination(int(right - left), int(top - bottom),
                                     int(left - target.x), int(bottom - target.y));
        const int x = int(left - west), y = int(bottom - south);
        if (!groups) {
            auto stream = map.createRange(start, bytes);
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
                const auto entry = readRecord<Group::Bytes>(*index);
                const uint32_t number = get(entry.data()), count = get(entry.data() + Group::BlockCount);
                const uint32_t offset = get(entry.data() + Group::DataOffset), length = get(entry.data() + Group::DataBytes);
                check(number == next_block && count && uint64_t(number) + count <= blocks
                      && offset == next_offset && length && uint64_t(offset) + length <= end);
                next_block += count;
                next_offset += length;
                if (!intersectsBlocks(number, count, columns, x0, x1, y0, y1)) continue;
                if (!stream) {
                    stream = map.createRange(offset, static_cast<size_t>(end - offset));
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
    check(index->pos() == map_end && previous_end == value(index_format::Header::MapBytes));
}
} // namespace

uint32_t DiskIndexedMapReader::value(size_t offset) const { return index_format::get(info_.header.data() + offset); }

void DiskIndexedMapReader::readRect(const galuchat::Rect& target, uint32_t* pixels) const {
    if (target.width <= 0 || target.height <= 0 || !pixels ||
        uint64_t(target.width) * target.height > SIZE_MAX / sizeof(uint32_t))
        throw std::invalid_argument("invalid map rectangle");
    std::fill_n(pixels, size_t(target.width) * target.height, 0);
    readRectangle(info_, *map_, *index_, target, [&](int width, int height, int dx, int dy) {
        return Destination{width, height, dx, dy, target.width, pixels};
    });
}

void DiskIndexedMapReader::readRectDecimated(const galuchat::Rect& target, unsigned scale, uint32_t* pixels) const {
    if (scale == 1) { readRect(target, pixels); return; }
    if ((scale != 2 && scale != 4) || target.width <= 0 || target.height <= 0 || !pixels)
        throw std::invalid_argument("invalid decimated map rectangle");
    const int width = 1 + (target.width - 1) / int(scale);
    const int height = 1 + (target.height - 1) / int(scale);
    if (uint64_t(width) * height > SIZE_MAX / sizeof(uint32_t))
        throw std::invalid_argument("decimated map rectangle too large");
    std::fill_n(pixels, size_t(width) * height, 0);
    readRectangle(info_, *map_, *index_, target, [&](int w, int h, int dx, int dy) {
        return DecimatedDestination{w, h, dx, dy, width, scale, pixels};
    });
}
} // namespace geoputer

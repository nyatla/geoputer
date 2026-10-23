#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <galuchat/wgsmapset3_indexed_reader.hpp>
#include "task/storage/StorageActivity.h"
#include "Config.h"

namespace geoputer {

struct RamMapCacheStats {
    uint32_t hits = 0;
    uint32_t misses = 0;
    uint32_t fills = 0;
    uint32_t failures = 0;
    uint64_t last_rect_us = 0;
    bool valid = false;
    size_t bytes = 0;
    galuchat::Rect bounds{};
};

// Storage-only point interface backed by a single, lazily allocated rectangle.
class RamCachedMapReader {
public:
    static constexpr int Side = config::MapCacheSide;
    static constexpr size_t Pixels = Side * Side;
    static constexpr size_t Bytes = Pixels * sizeof(uint32_t);

    explicit RamCachedMapReader(std::shared_ptr<const galuchat::ReaderFactory> factory, StorageObserver* observer = nullptr)
        : reader_(std::move(factory), config::MaxDatasetChunks), observer_(observer) {}

    // Code zero includes uncovered cells and undefined regions, as PlaceLookup
    // already treats both as NotFound. No point/file reads are made on a hit.
    uint32_t readWgsPoint(double longitude, double latitude);
    bool isCached(double longitude, double latitude) const;
    void invalidate() { stats_.valid = false; if (observer_) observer_->cacheState(RgCacheState::Unprepared); }
    const RamMapCacheStats& stats() const { return stats_; }
    size_t chunkCount() const { return reader_.mapCount(); }
    galuchat::DoubleRect coverage() const { return reader_.areaOfWgs(); }
    int unitInvX() const { return reader_.unit_inv_x; }
    int unitInvY() const { return reader_.unit_inv_y; }

private:
    bool contains(int x, int y) const;
    void rebuild(int x, int y);

    galuchat::GaluchatWGSMapSet3IndexedReader reader_;
    std::unique_ptr<uint32_t[]> pixels_;
    RamMapCacheStats stats_{};
    StorageObserver* observer_;
};

static_assert(RamCachedMapReader::Bytes == config::MapCacheSide * config::MapCacheSide * sizeof(uint32_t),
              "One raw uint32 cache image");

}  // namespace geoputer

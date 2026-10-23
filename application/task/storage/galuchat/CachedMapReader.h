#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include "DiskIndexedMapReader.h"
#include "../StorageActivity.h"
#include "../../../Config.h"

namespace geoputer {

struct MapCacheStats {
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
class CachedMapReader {
public:
    static constexpr int Side = config::MapCacheSide;
    static constexpr size_t Pixels = Side * Side;
    static constexpr size_t Bytes = Pixels * sizeof(uint32_t);

    CachedMapReader(DatasetMapIndex info,std::shared_ptr<const galuchat::ReaderFactory> map,
                    std::shared_ptr<const galuchat::ReaderFactory> index,StorageObserver* observer=nullptr)
        : reader_(info,std::move(map),std::move(index)),observer_(observer) {}

    // Code zero includes uncovered cells and undefined regions, as PlaceLookup
    // already treats both as NotFound. No point/file reads are made on a hit.
    uint32_t readWgsPoint(double longitude, double latitude);
    bool isCached(double longitude, double latitude) const;
    void invalidate() { stats_.valid = false; if (observer_) observer_->cacheState(RgCacheState::Unprepared); }
    const MapCacheStats& stats() const { return stats_; }
    size_t chunkCount() const { return reader_.chunkCount(); }
    galuchat::DoubleRect coverage() const { return reader_.coverage(); }
    int unitInvX() const { return reader_.unitInvX(); }
    int unitInvY() const { return reader_.unitInvY(); }

private:
    bool contains(int x, int y) const;
    void rebuild(int x, int y);

    DiskIndexedMapReader reader_;
    std::unique_ptr<uint32_t[]> pixels_;
    MapCacheStats stats_{};
    StorageObserver* observer_;
};

static_assert(CachedMapReader::Bytes == config::MapCacheSide * config::MapCacheSide * sizeof(uint32_t),
              "One raw uint32 cache image");

}  // namespace geoputer

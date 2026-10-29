#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <stdexcept>
#include "DiskIndexedMapReader.h"
#include "../StorageActivity.h"
#include "../../../Config.h"

namespace geoputer {

using CodeCacheBuffer=std::array<uint32_t,size_t(config::MapCacheSide)*config::MapCacheSide>;

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

// Storage-only point interface backed by its owner's fixed full-resolution array.
class CachedMapReader {
public:
    static constexpr int Side = config::MapCacheSide;
    static constexpr size_t Pixels = Side * Side;
    static constexpr size_t Bytes = Pixels * sizeof(uint32_t);

    CachedMapReader(CodeCacheBuffer& pixels,DatasetMapIndex info,ScopedReaderSource map,
                    ScopedReaderSource index,StorageObserver* observer=nullptr,unsigned side=Side)
        :reader_(info,map,index),pixels_(pixels),observer_(observer),side_(side) {
        if(!side_ || side_>Side)throw std::invalid_argument("invalid code cache side");
    }

    // Code zero includes uncovered cells and undefined regions, as PlaceLookup
    // already treats both as NotFound. No point/file reads are made on a hit.
    uint32_t readWgsPoint(double longitude, double latitude,BorrowedReadBuffer* work=nullptr);
    bool isCached(double longitude, double latitude) const;
    void invalidate() { stats_.valid = false; if (observer_) observer_->cacheState(RgCacheState::Unprepared); }
    const MapCacheStats& stats() const { return stats_; }
    size_t chunkCount() const { return reader_.chunkCount(); }
    int unitInvX() const { return reader_.unitInvX(); }
    int unitInvY() const { return reader_.unitInvY(); }

private:
    bool contains(int x, int y) const;
    void rebuild(int x, int y,BorrowedReadBuffer* work);

    DiskIndexedMapReader reader_;
    CodeCacheBuffer& pixels_;
    MapCacheStats stats_{};
    StorageObserver* observer_;
    unsigned side_;
};

static_assert(CachedMapReader::Bytes == config::MapCacheSide * config::MapCacheSide * sizeof(uint32_t),
              "One raw uint32 cache image");

}  // namespace geoputer

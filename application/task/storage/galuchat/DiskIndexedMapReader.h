#pragma once
#include <galuchat/wgsmap3_reader.hpp>
#include "../dataset/DatasetIndex.h"
#include "ScopedReaderSource.h"
namespace geoputer {
// Owns validated metadata; both input descriptions are borrowed from the caller.
class DiskIndexedMapReader {
public:
    DiskIndexedMapReader(DatasetMapIndex info,ScopedReaderSource map,ScopedReaderSource index)
        :info_(info),map_(map),index_(index){}
    void readRect(const galuchat::Rect& target,uint32_t* pixels,BorrowedReadBuffer* work=nullptr) const;
    // Nearest-neighbor decimation before edge extraction. Supports 1, 2, 4.
    void readRectDecimated(const galuchat::Rect& target,unsigned scale,uint32_t* pixels,BorrowedReadBuffer* work=nullptr) const;
    uint32_t chunkCount() const { return value(index_format::Header::ChunkCount); }
    int unitInvX() const { return static_cast<int>(value(index_format::Header::UnitInvX)); }
    int unitInvY() const { return static_cast<int>(value(index_format::Header::UnitInvY)); }
private:
    uint32_t value(size_t offset) const;
    DatasetMapIndex info_;
    ScopedReaderSource map_,index_;
};
}

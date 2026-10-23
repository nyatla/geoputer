#pragma once
#include <memory>
#include <galuchat/wgsmap3_reader.hpp>
#include "../dataset/DatasetIndex.h"
namespace geoputer {
// Holds one validated metadata summary, never an array of chunk entries.
class DiskIndexedMapReader {
public:
    DiskIndexedMapReader(DatasetMapIndex info,std::shared_ptr<const galuchat::ReaderFactory> map,
                         std::shared_ptr<const galuchat::ReaderFactory> index)
        : info_(info),map_(std::move(map)),index_(std::move(index)) {}
    void readRect(const galuchat::Rect& target,uint32_t* pixels) const;
    uint32_t chunkCount() const { return value(36); }
    int unitInvX() const { return static_cast<int>(value(40)); }
    int unitInvY() const { return static_cast<int>(value(44)); }
    galuchat::DoubleRect coverage() const {
        return {double(info_.west)/unitInvX(),double(info_.south)/unitInvY(),
                double(info_.width)/unitInvX(),double(info_.height)/unitInvY()};
    }
private:
    uint32_t value(size_t offset) const;
    DatasetMapIndex info_;
    std::shared_ptr<const galuchat::ReaderFactory> map_,index_;
};
}

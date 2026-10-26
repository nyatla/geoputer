#pragma once
#include "MapTypes.h"
#include <algorithm>
#include <cstdint>

namespace geoputer::map {
// Borrowed packed pixels. No allocation, deletion, or source-raster ownership.
class BoundaryImageView {
public:
    BoundaryImageView(uint8_t* data=nullptr,unsigned width=0,unsigned height=0)
        : data_(data),width_(width),height_(height) {}
    unsigned width() const {return width_;}
    unsigned height() const {return height_;}
    size_t stride() const {return (width_+3)/4;}
    size_t bytes() const {return stride()*height_;}
    uint8_t* data() {return data_;}
    const uint8_t* data() const {return data_;}
    uint8_t get(unsigned x,unsigned y) const {return (data_[y*stride()+x/4]>>(6-2*(x%4)))&3;}
    void set(unsigned x,unsigned y,uint8_t value) {
        auto& byte=data_[y*stride()+x/4];const unsigned shift=6-2*(x%4);
        byte=uint8_t((byte&~(3u<<shift))|((value&3u)<<shift));
    }
private:
    uint8_t* data_;
    unsigned width_,height_;
};
// UI-owned fixed image. Its address remains valid across screen changes.
class FixedBoundaryImage : public BoundaryImageView {
public:
    static constexpr size_t Capacity=BoundaryBytes;
    FixedBoundaryImage()=default;
    FixedBoundaryImage(const FixedBoundaryImage&)=delete;
    FixedBoundaryImage& operator=(const FixedBoundaryImage&)=delete;
    // Bind storage without clearing it. Only a completed writer may publish it.
    bool bind(unsigned width,unsigned height) {
        if(!width || !height || width>MaxWidth || height>MaxHeight)return false;
        BoundaryImageView::operator=({storage_,width,height});
        return true;
    }
    void clear() {BoundaryImageView::operator=({});}
    uint8_t* storageData() {return storage_;}
private:
    alignas(std::max_align_t) uint8_t storage_[Capacity];
};
} // namespace geoputer::map

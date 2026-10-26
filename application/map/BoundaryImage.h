#pragma once
#include "BoundaryImageView.h"
#include <algorithm>
#include <memory>
#include <new>
#include <utility>

namespace geoputer::map {
// Color-independent pixels: 0 background, 1 boundary, 2 current-region boundary.
class BoundaryImage {
public:
    bool resize(unsigned width, unsigned height) {
        if (!width || !height || width > MaxWidth || height > MaxHeight) return false;
        const size_t bytes = ((width + 3) / 4) * height;
        if (bytes != size_) {
            std::unique_ptr<uint8_t[]> next(new (std::nothrow) uint8_t[bytes]);
            if (!next) return false;
            data_ = std::move(next); size_ = bytes;
        }
        width_ = width; height_ = height;
        std::fill_n(data_.get(), size_, 0); return true;
    }
    void clear() { data_.reset(); width_ = height_ = 0; size_ = 0; }
    unsigned width() const { return width_; }
    unsigned height() const { return height_; }
    size_t stride() const { return (width_ + 3) / 4; }
    size_t bytes() const { return size_; }
    uint8_t* data() { return data_.get(); }
    const uint8_t* data() const { return data_.get(); }
    uint8_t get(unsigned x, unsigned y) const { return view().get(x,y); }
    void set(unsigned x, unsigned y, uint8_t value) { view().set(x,y,value); }
    void swap(BoundaryImage& other) {
        std::swap(data_, other.data_); std::swap(width_, other.width_);
        std::swap(height_, other.height_); std::swap(size_, other.size_);
    }
private:
    BoundaryImageView view() const {return {data_.get(),width_,height_};}
    std::unique_ptr<uint8_t[]> data_;
    unsigned width_ = 0, height_ = 0;
    size_t size_ = 0;
};
} // namespace geoputer::map

#pragma once
#include "ViewportTransform.h"
#include <memory>

namespace geoputer::map {
// Screen-lifetime raster, independent of any accepted rendering job. Only the
// Storage task accesses it; UI owns its own small, completed boundary image.
struct MapSession {
    FrameInfo info;
    ViewportPlan plan;
    std::unique_ptr<RegionCode[]> pixels;
    size_t capacity=0;
    bool valid=false;
    RasterView view() const {return {pixels.get(),plan.source,size_t(plan.source.width)*plan.source.height};}
    size_t bytes() const {return capacity*sizeof(RegionCode);}
    void clear() {*this=MapSession{};}
    void invalidate(const Request& request) {valid=false;info={};info.request=request;}
    void reserve(size_t count) {
        if(pixels && count<=capacity)return;
        // Never allocate the new large raster while the old one is still owned.
        pixels.reset();capacity=0;
        pixels.reset(new RegionCode[count]);capacity=count;
    }
    bool matches(const Request& r) const {
        const auto& old=info.request;
        return valid && pixels && old.session==r.session && old.screen_generation==r.screen_generation &&
            old.dataset_generation==r.dataset_generation && old.scale==r.scale &&
            old.latitude==r.latitude && old.longitude==r.longitude;
    }
    bool belongs(const Request& r) const {
        return info.request.session==r.session && info.request.screen_generation==r.screen_generation &&
            info.request.dataset_generation==r.dataset_generation;
    }
};
} // namespace geoputer::map

#pragma once
#include "ViewportTransform.h"
#include "../Config.h"
#include <array>
#include <new>

namespace geoputer::map {
using CodeImageBuffer=std::array<RegionCode,config::MapViewCodeMaxBytes/sizeof(RegionCode)>;
static_assert(config::MapViewCodeMaxBytes%sizeof(RegionCode)==0,"Whole region codes");
// Metadata borrows Storage's one fixed map array. Live and Log keep independent
// identities, but selectMap invalidates the other before either writes pixels.
struct MapSession {
    FrameInfo info{};
    ViewportPlan plan{};
    RegionCode* pixels=nullptr;
    size_t capacity=0;
    bool valid=false;
    MapSession()=default;
    explicit MapSession(CodeImageBuffer& storage) {bind(storage);}
    void bind(CodeImageBuffer& storage) {clear();pixels=storage.data();capacity=storage.size();}
    void unbind() {clear();pixels=nullptr;capacity=0;}
    MapSession(const MapSession&)=delete;
    MapSession& operator=(const MapSession&)=delete;
    MapSession(MapSession&& other) noexcept {moveFrom(other);}
    MapSession& operator=(MapSession&& other) noexcept {if(this!=&other)moveFrom(other);return *this;}
    RasterView view() const {return {pixels,plan.source,size_t(plan.source.width)*plan.source.height};}
    size_t bytes() const {return valid?view().count*sizeof(RegionCode):0;}
    void clear() {info={};plan={};valid=false;} // Binding survives dataset/viewport invalidation.
    void invalidate(const Request& request) {clear();info.request=request;}
    void reserve(size_t count) const {
        if(!pixels || count>capacity)throw std::bad_alloc();
    }
    bool samePixels(const Request& r) const {
        const auto& old=info.request;
        return valid && pixels && old.session==r.session && old.dataset_generation==r.dataset_generation &&
            old.scale==r.scale && old.latitude==r.latitude && old.longitude==r.longitude;
    }
    bool matches(const Request& r) const {return samePixels(r) && info.request.screen_generation==r.screen_generation;}
    bool belongs(const Request& r) const {
        return info.request.session==r.session && info.request.screen_generation==r.screen_generation &&
            info.request.dataset_generation==r.dataset_generation;
    }
private:
    void moveFrom(MapSession& other) {
        info=other.info;plan=other.plan;pixels=other.pixels;capacity=other.capacity;valid=other.valid;
        other.clear(); // Both objects retain a non-owning binding; only the result is valid.
    }
};
} // namespace geoputer::map

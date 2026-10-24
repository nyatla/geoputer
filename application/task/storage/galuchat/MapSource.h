#pragma once
#include "../../../map/ViewportTransform.h"
#include "../StorageActivity.h"
#include <memory>
#include <string>

namespace geoputer {
struct MapSourceResult {
    map::FrameInfo info;
    map::ViewportPlan plan;
    std::unique_ptr<map::RegionCode[]> pixels;
    map::RasterView view() const { return {pixels.get(),plan.source,size_t(plan.source.width)*plan.source.height}; }
};
// Independent reader session. Never accesses or invalidates CachedMapReader.
MapSourceResult readBoundaryMap(const std::string& directory, const std::string& language,
                                const map::Request& request, uint8_t drive, StorageObserver* observer);
} // namespace geoputer

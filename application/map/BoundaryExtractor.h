#pragma once
#include "BoundaryImage.h"
#include "ViewportTransform.h"

namespace geoputer::map {
inline Error buildBoundaryRows(const RasterView& source, const ViewportPlan& plan, RegionCode current,
                               unsigned first, unsigned rows, BoundaryImage& image) {
    if (plan.width<=0 || plan.height<=0 || !std::isfinite(plan.grid_rows_per_pixel) || plan.grid_rows_per_pixel<=0 || !source.pixels || source.bounds.width <= 0 || source.bounds.height <= 0 ||
        uint64_t(source.bounds.width) * source.bounds.height > source.count ||
        image.width() != unsigned(plan.width) || image.height() != unsigned(plan.height) ||
        first > image.height() || rows > image.height() - first) return Error::InvalidInput;
    auto contains = [&](int x, int y) { return int64_t(x) >= source.bounds.x && int64_t(y) >= source.bounds.y &&
        int64_t(x) < int64_t(source.bounds.x) + source.bounds.width && int64_t(y) < int64_t(source.bounds.y) + source.bounds.height; };
    if (!rows) return Error::None;
    if (!contains(plan.sampleX(0),plan.sampleY(first)) ||
        !contains(plan.sampleX(plan.width-1),plan.sampleY(std::min<unsigned>(plan.height-1,first+rows)))) return Error::InvalidInput;
    for (unsigned y = first; y < first + rows; ++y) for (int x = 0; x < plan.width; ++x) {
        const auto a = source.get(plan.sampleX(x),plan.sampleY(y));
        const auto b = x+1 < plan.width ? source.get(plan.sampleX(x+1),plan.sampleY(y)) : a;
        const auto c = y+1 < unsigned(plan.height) ? source.get(plan.sampleX(x),plan.sampleY(y+1)) : a;
        image.set(x,y,a != b || a != c ? current && (a == current || b == current || c == current) ? 2 : 1 : 0);
    }
    return Error::None;
}
} // namespace geoputer::map

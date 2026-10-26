#pragma once
#include "BoundaryImage.h"
#include "ViewportTransform.h"

namespace geoputer::map {
template<class Image>
inline Error buildBoundaryRows(const RasterView& source, const ViewportPlan& plan, RegionCode current,
                               unsigned first, unsigned rows, Image& image) {
    if (plan.width<=0 || plan.height<=0 || !std::isfinite(plan.grid_rows_per_pixel) || plan.grid_rows_per_pixel<=0 || !source.pixels || source.bounds.width <= 0 || source.bounds.height <= 0 ||
        uint64_t(source.bounds.width) * source.bounds.height > source.count ||
        image.width() != unsigned(plan.width) || image.height() != unsigned(plan.height) ||
        first > image.height() || rows > image.height() - first) return Error::InvalidInput;
    auto contains = [&](int x, int y) { return int64_t(x) >= source.bounds.x && int64_t(y) >= source.bounds.y &&
        int64_t(x) < int64_t(source.bounds.x) + source.bounds.width && int64_t(y) < int64_t(source.bounds.y) + source.bounds.height; };
    if (!rows) return Error::None;
    if (!contains(plan.sampleX(0),plan.sampleY(first)) ||
        !contains(plan.sampleX(plan.width-1),plan.sampleY(std::min<unsigned>(plan.height-1,first+rows)))) return Error::InvalidInput;
    // Sampling is constant across a row. Carry the next sampled Y forward so
    // floating-point rounding runs once per row, rather than for every pixel.
    const size_t source_x = size_t(int64_t(plan.sampleX(0)) - source.bounds.x);
    int sample_y = plan.sampleY(first);
    for (unsigned y = first; y < first + rows; ++y) {
        const int next_y = y + 1 < unsigned(plan.height) ? plan.sampleY(y + 1) : sample_y;
        const auto* row = source.pixels + size_t(int64_t(sample_y) - source.bounds.y) * source.bounds.width + source_x;
        const auto* next_row = source.pixels + size_t(int64_t(next_y) - source.bounds.y) * source.bounds.width + source_x;
        for (int x = 0; x < plan.width; ++x) {
            const auto a = row[x];
            const auto b = x + 1 < plan.width ? row[x + 1] : a;
            const auto c = next_row[x];
            image.set(x,y,a != b || a != c ? current && (a == current || b == current || c == current) ? 2 : 1 : 0);
        }
        sample_y = next_y;
    }
    return Error::None;
}
} // namespace geoputer::map

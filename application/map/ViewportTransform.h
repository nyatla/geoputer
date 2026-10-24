#pragma once
#include "MapTypes.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace geoputer::map {
struct ViewportPlan {
    GridRect source;
    int width = 0, height = 0, center_x = 0, center_y = 0;
    double meters_per_pixel = 0, grid_rows_per_pixel = 0;
    int sampleX(int x) const { return center_x + x - width / 2; }
    int sampleY(int y) const { return center_y + int(std::lround((height / 2 - y) * grid_rows_per_pixel)); }
};
inline Error planViewport(double latitude, double longitude, int unit_x, int unit_y,
                          size_t max_bytes, ViewportPlan& out, int width = MaxWidth, int height = MaxHeight) {
    if (!std::isfinite(latitude) || !std::isfinite(longitude) || std::abs(latitude) >= 89.9 ||
        std::abs(longitude) > 180 || unit_x <= 0 || unit_y <= 0 || width <= 0 || height <= 0 ||
        width > int(MaxWidth) || height > int(MaxHeight)) return Error::InvalidInput;
    ViewportPlan p; p.width = width; p.height = height;
    const double gx = std::round(longitude * unit_x), gy = std::round(latitude * unit_y);
    if (gx < INT32_MIN + width || gx > INT32_MAX - width || gy < INT32_MIN + height || gy > INT32_MAX - height)
        return Error::InvalidInput;
    p.center_x = int(gx); p.center_y = int(gy);
    p.meters_per_pixel = 111320 * std::cos(latitude * 3.14159265358979323846 / 180) / unit_x;
    p.grid_rows_per_pixel = p.meters_per_pixel / (111320.0 / unit_y);
    const double span = std::ceil(height * p.grid_rows_per_pixel);
    if (!std::isfinite(span) || span > INT32_MAX / 2 || gy - span < INT32_MIN || gy + span > INT32_MAX)
        return Error::TooLarge;
    const int south = p.sampleY(height - 1), north = p.sampleY(0);
    p.source = {p.sampleX(0), south, width, north - south + 1};
    const uint64_t bytes = uint64_t(width) * p.source.height * sizeof(RegionCode);
    if (bytes > max_bytes) return Error::TooLarge;
    out = p; return Error::None;
}
} // namespace geoputer::map

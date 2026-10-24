#pragma once
#include "ViewportTransform.h"

namespace geoputer::map {
struct DecimatedViewport {
    // The stored grid uses the original viewport coordinates. Its samples map
    // to a wider input lattice centered on the exact same current-location cell.
    ViewportPlan output;
    GridRect input;
    unsigned scale = 1;
};
inline Error planDecimatedViewport(double latitude, double longitude, int unit_x, int unit_y,
                                   size_t max_bytes, unsigned scale, DecimatedViewport& out,
                                   int width = MaxWidth, int height = MaxHeight) {
    if (!supportedScale(scale)) return Error::InvalidInput;
    DecimatedViewport p;
    const auto error = planViewport(latitude, longitude, unit_x, unit_y, max_bytes, p.output, width, height);
    if (error != Error::None) return error;
    const auto& v = p.output;
    const int64_t x = int64_t(v.center_x) + (int64_t(v.source.x) - v.center_x) * scale;
    const int64_t y = int64_t(v.center_y) + (int64_t(v.source.y) - v.center_y) * scale;
    // Include exactly the first through last selected sample, without padding.
    const int64_t w = (int64_t(v.source.width) - 1) * scale + 1;
    const int64_t h = (int64_t(v.source.height) - 1) * scale + 1;
    if (x < INT32_MIN || y < INT32_MIN || w > INT32_MAX || h > INT32_MAX ||
        x + w > INT32_MAX || y + h > INT32_MAX) return Error::TooLarge;
    p.input = {int(x), int(y), int(w), int(h)};
    p.scale = scale;
    p.output.meters_per_pixel *= scale;
    out = p;
    return Error::None;
}
} // namespace geoputer::map

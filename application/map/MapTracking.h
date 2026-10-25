#pragma once
#include "MapTypes.h"
#include <cmath>

namespace geoputer::map {
// Geometry of the image actually committed to the display, not a pending job.
struct TrackingAnchor {
    Request request;
    double meters_per_pixel = 0;
};
struct CursorOffset {
    double x = 0, y = 0;
    bool valid = false;
};
inline CursorOffset cursorOffset(const TrackingAnchor& anchor, double latitude, double longitude) {
    const auto& base=anchor.request;
    if(!supportedScale(base.scale) || !std::isfinite(anchor.meters_per_pixel) || anchor.meters_per_pixel<=0 ||
       !std::isfinite(base.latitude) || std::abs(base.latitude)>=89.9 ||
       !std::isfinite(base.longitude) || std::abs(base.longitude)>180 ||
       !std::isfinite(latitude) || std::abs(latitude)>90 || !std::isfinite(longitude) || std::abs(longitude)>180)return {};
    const double east=std::remainder(longitude-base.longitude,360.0)*111320.0*std::cos(base.latitude*3.14159265358979323846/180);
    const double north=(latitude-base.latitude)*111320.0;
    return {east/anchor.meters_per_pixel,-north/anchor.meters_per_pixel,true};
}
inline bool inTrackingWindow(const CursorOffset& cursor,unsigned radius) {
    // Allow floating-point roundoff at an exact integer-pixel boundary only.
    return cursor.valid && std::abs(cursor.x)<=radius+1e-7 && std::abs(cursor.y)<=radius+1e-7;
}
} // namespace geoputer::map

#include "CachedMapReader.h"
#include "../../../utils/ElapsedTime.h"
#include <limits>
#include <stdexcept>

namespace geoputer {
namespace {
// WGSMapSet readWgsPoint returns the first nonzero overlapping map, while
// readWgsRect normally overwrites with later maps. Preserve the point priority
// in the destination adapter. SDK's initial zero-fill clears the entire buffer.
struct PointPriorityRaster {
    int width = CachedMapReader::Side;
    int height = CachedMapReader::Side;
    uint32_t* pixels;

    void set(int x, int y, int64_t value) {
        if (value < 0 || static_cast<uint64_t>(value) > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("region code exceeds uint32 cache range");
        auto& pixel = pixels[static_cast<size_t>(y) * width + x];
        if (value == 0 || pixel == 0) pixel = static_cast<uint32_t>(value);
    }
};
}  // namespace

bool CachedMapReader::contains(int x, int y) const {
    const int64_t dx = static_cast<int64_t>(x) - stats_.bounds.x;
    const int64_t dy = static_cast<int64_t>(y) - stats_.bounds.y;
    return stats_.valid && dx >= 0 && dy >= 0 && dx < Side && dy < Side;
}

void CachedMapReader::rebuild(int x, int y) {
    ++stats_.misses;
    stats_.last_rect_us = 0;
    stats_.valid = false;
    if (observer_) observer_->cacheState(RgCacheState::Rebuilding);
    try {
        const galuchat::Rect target{
            galuchat::detail::subtractInt(x, Side / 2, "cache west"),
            galuchat::detail::subtractInt(y, Side / 2, "cache south"), Side, Side};
        if (!pixels_) {
            pixels_.reset(new uint32_t[Pixels]);
            stats_.bytes = Bytes;
        }
        PointPriorityRaster destination{Side, Side, pixels_.get()};
        {
            ElapsedTime elapsed(stats_.last_rect_us);
            reader_.readWgsRect(target, destination);
        }
        stats_.bounds = target;
        stats_.valid = true;
        ++stats_.fills;
        if (observer_) observer_->cacheState(RgCacheState::Ready);
    } catch (...) {
        ++stats_.failures;
        if (observer_) observer_->cacheState(RgCacheState::Unprepared);
        throw;  // Partially rewritten pixels stay invalid until a successful refill.
    }
}

uint32_t CachedMapReader::readWgsPoint(double longitude, double latitude) {
    const int x = galuchat::detail::roundedInt(longitude * reader_.unit_inv_x, "cache longitude");
    const int y = galuchat::detail::roundedInt(latitude * reader_.unit_inv_y, "cache latitude");
    if (contains(x, y)) ++stats_.hits;
    else rebuild(x, y);
    const size_t offset = static_cast<size_t>(y - stats_.bounds.y) * Side + (x - stats_.bounds.x);
    return pixels_[offset];
}

bool CachedMapReader::isCached(double longitude, double latitude) const {
    const int x = galuchat::detail::roundedInt(longitude * reader_.unit_inv_x, "cache longitude");
    const int y = galuchat::detail::roundedInt(latitude * reader_.unit_inv_y, "cache latitude");
    return contains(x, y);
}

}  // namespace geoputer

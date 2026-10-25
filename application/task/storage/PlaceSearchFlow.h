#pragma once

#include <cstdint>
#include <cmath>
#include "PlaceTypes.h"

namespace geoputer {

// App-owned scheduling only; no SDK calls or shared task data.
class PlaceSearchFlow {
public:
    void refresh() { force_ = true; if (pending()) pending_invalidated_ = true; }
    bool owns(uint32_t id) const { return id && id == pending_id_; }
    bool pending() const { return pending_id_ != 0; }
    bool accepts(uint32_t id, uint32_t result_generation, uint32_t current_generation) const {
        return pending() && id == pending_id_ && !pending_invalidated_ &&
               result_generation == pending_generation_ && result_generation == current_generation;
    }
    // A completed lookup covers its dataset cell, independently of map zoom or
    // the displayed image's tracking window. Match Galuchat's round-to-nearest.
    bool sameCell(const GpsFix& fix, int unit_x, int unit_y) const {
        return !force_ && !pending() && unit_x>0 && unit_y>0 && fix.valid && last_fix_valid_ &&
            std::isfinite(fix.longitude) && std::isfinite(fix.latitude) &&
            std::round(fix.longitude*unit_x)==std::round(last_longitude_*unit_x) &&
            std::round(fix.latitude*unit_y)==std::round(last_latitude_*unit_y);
    }
    uint32_t waitMs(uint32_t, uint32_t sample) const {
        return pending() || (!force_ && sample==last_sample_) ? UINT32_MAX : 0;
    }
    uint32_t waitMs(uint32_t now,uint32_t sample,const GpsFix& fix,int unit_x,int unit_y) const {
        return sameCell(fix,unit_x,unit_y) ? UINT32_MAX : waitMs(now,sample);
    }
    void sent(uint32_t id, const PlaceRequest& request, uint32_t) {
        pending_id_ = id; pending_generation_ = request.screen_generation;
        last_sample_ = request.sample_seq;
        last_latitude_ = request.fix.latitude; last_longitude_ = request.fix.longitude; last_fix_valid_ = request.fix.valid;
        force_ = false;
        pending_invalidated_ = false;
    }
    // A stale view still has to consume its completion before sending new work.
    bool completed(uint32_t id, uint32_t result_generation, uint32_t current_generation) {
        if (!pending() || id != pending_id_) return false;
        const bool current = accepts(id, result_generation, current_generation);
        pending_id_ = 0;
        return current;
    }
private:
    uint32_t pending_id_ = 0, pending_generation_ = 0;
    uint32_t last_sample_ = 0;
    double last_latitude_=0,last_longitude_=0;
    bool last_fix_valid_=false;
    bool force_ = true;
    bool pending_invalidated_ = false;
};

}  // namespace geoputer

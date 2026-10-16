#pragma once

#include <cstdint>
#include "PlaceTypes.h"
#include "../../Config.h"

namespace geoputer {

// App-owned scheduling only; no SDK calls or shared task data.
class PlaceSearchFlow {
public:
    static constexpr uint32_t IntervalMs = config::PlaceSearchIntervalMs;
    void refresh() { force_ = true; if (pending()) pending_invalidated_ = true; }
    bool pending() const { return pending_id_ != 0; }
    bool accepts(uint32_t id, uint32_t result_generation, uint32_t current_generation) const {
        return pending() && id == pending_id_ && !pending_invalidated_ &&
               result_generation == pending_generation_ && result_generation == current_generation;
    }
    uint32_t waitMs(uint32_t now, uint32_t sample) const {
        if (pending() || (!force_ && sample == last_sample_)) return UINT32_MAX;
        if (force_ || !started_) return 0;
        const uint32_t elapsed = now - last_start_;
        return elapsed >= IntervalMs ? 0 : IntervalMs - elapsed;
    }
    void sent(uint32_t id, const PlaceRequest& request, uint32_t now) {
        pending_id_ = id; pending_generation_ = request.screen_generation;
        last_sample_ = request.sample_seq; last_start_ = now;
        force_ = false; started_ = true;
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
    uint32_t last_sample_ = 0, last_start_ = 0;
    bool force_ = true, started_ = false;
    bool pending_invalidated_ = false;
};

}  // namespace geoputer

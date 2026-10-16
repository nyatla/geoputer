#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace geoputer {

// UI-owned display clock. GPS supplies its initial UTC; monotonic ticks keep
// it running independently of position validity and subsequent GPS messages.
class DisplayClock {
public:
    static constexpr uint32_t DayMs = 86400000u;
    bool known() const { return known_; }

    bool setInitialUtc(uint32_t utc_ms, uint32_t now_ms) {
        if (known_ || utc_ms >= DayMs) return false;
        utc_ms_ = utc_ms;
        last_tick_ms_ = now_ms;
        known_ = true;
        return true;
    }

    void tick(uint32_t now_ms) {
        if (known_)
            utc_ms_ = (static_cast<uint64_t>(utc_ms_) + static_cast<uint32_t>(now_ms - last_tick_ms_)) % DayMs;
        last_tick_ms_ = now_ms;
    }

    uint32_t secondsJst() const {
        return known_ ? (utc_ms_ / 1000u + 9u * 3600u) % 86400u : UINT32_MAX;
    }

    void format(char* out, size_t size) const {
        if (!known_) { std::snprintf(out, size, "--:--:--"); return; }
        const auto seconds = secondsJst();
        std::snprintf(out, size, "%02u:%02u:%02u", static_cast<unsigned>(seconds / 3600u),
                      static_cast<unsigned>(seconds / 60u % 60u), static_cast<unsigned>(seconds % 60u));
    }

private:
    uint32_t utc_ms_ = 0;
    uint32_t last_tick_ms_ = 0;
    bool known_ = false;
};

}  // namespace geoputer

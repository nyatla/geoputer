#pragma once
#include <cstdint>
#include "../Config.h"
#ifdef ARDUINO_ARCH_ESP32
#include <esp_timer.h>
#else
#include <chrono>
#endif

namespace geoputer {
inline uint64_t monotonicMicros() {
#ifdef ARDUINO_ARCH_ESP32
    return static_cast<uint64_t>(esp_timer_get_time());
#else
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
// Diagnostic wall time on success, early return and exception. Normal builds
// perform no clock reads and leave the destination unchanged.
class ElapsedTime {
public:
    explicit ElapsedTime(uint64_t& elapsed) : elapsed_(elapsed), start_(config::DetailedDiagnostics ? monotonicMicros() : 0) {}
    ~ElapsedTime() { if constexpr (config::DetailedDiagnostics) elapsed_ = monotonicMicros() - start_; }
    ElapsedTime(const ElapsedTime&) = delete;
    ElapsedTime& operator=(const ElapsedTime&) = delete;
private:
    uint64_t& elapsed_;
    uint64_t start_;
};
} // namespace geoputer

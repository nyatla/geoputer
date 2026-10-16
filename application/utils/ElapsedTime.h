#pragma once
#include <cstdint>
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
// Record wall time on success, early return, and exception, before any logging.
class ElapsedTime {
public:
    explicit ElapsedTime(uint64_t& elapsed) : elapsed_(elapsed), start_(monotonicMicros()) {}
    ~ElapsedTime() { elapsed_ = monotonicMicros() - start_; }
    ElapsedTime(const ElapsedTime&) = delete;
    ElapsedTime& operator=(const ElapsedTime&) = delete;
private:
    uint64_t& elapsed_;
    uint64_t start_;
};
} // namespace geoputer

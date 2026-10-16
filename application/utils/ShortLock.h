#pragma once

#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#else
#include <mutex>
#endif

namespace geoputer {
// Only bounded RAM operations belong inside this lock, never file I/O or queue waits.
class ShortLock {
public:
    class Guard {
    public:
        explicit Guard(ShortLock& lock) : lock_(lock) { lock_.enter(); }
        ~Guard() { lock_.leave(); }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
    private:
        ShortLock& lock_;
    };
private:
    void enter() {
#ifdef ARDUINO_ARCH_ESP32
        portENTER_CRITICAL(&mux_);
#else
        mutex_.lock();
#endif
    }
    void leave() {
#ifdef ARDUINO_ARCH_ESP32
        portEXIT_CRITICAL(&mux_);
#else
        mutex_.unlock();
#endif
    }
#ifdef ARDUINO_ARCH_ESP32
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
#else
    std::mutex mutex_;
#endif
};
}  // namespace geoputer

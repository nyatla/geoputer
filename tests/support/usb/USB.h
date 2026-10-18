#pragma once
#include <atomic>
struct HostUSB {
    inline static std::atomic<bool> mounted{false}, present{false};
    explicit operator bool() const { return mounted; }
};
inline HostUSB USB;

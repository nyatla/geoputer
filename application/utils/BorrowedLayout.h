#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace geoputer {
// Overlay a named layout on borrowed byte storage. The owner controls its
// lifetime; get() is valid only between construct() and destroy(). No allocation.
template<class Layout> struct BorrowedLayout {
    static_assert(std::is_standard_layout<Layout>::value,"Borrowed work must have a defined member layout");
    static_assert(std::is_trivially_destructible<Layout>::value,"Borrowed work must not own resources");
    static bool fits(const uint8_t* memory,size_t capacity) {
        return memory && capacity>=sizeof(Layout) && reinterpret_cast<uintptr_t>(memory)%alignof(Layout)==0;
    }
    static Layout* construct(uint8_t* memory,size_t capacity) {
        if(!fits(memory,capacity))throw std::invalid_argument("borrowed layout does not fit");
        // Default initialization deliberately leaves byte arrays untouched.
        return ::new (static_cast<void*>(memory)) Layout;
    }
    static Layout* get(uint8_t* memory,size_t capacity) {
        assert(fits(memory,capacity));
        (void)capacity;
        return std::launder(static_cast<Layout*>(static_cast<void*>(memory)));
    }
    static void destroy(uint8_t* memory,size_t capacity) {get(memory,capacity)->~Layout();}
};
} // namespace geoputer

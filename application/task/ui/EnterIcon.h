#pragma once
#include <cstdint>
namespace geoputer::enter_icon {
// Native framed keycap, centered in the 14-pixel footer. Constant ROM rows.
inline constexpr unsigned Width=16, Height=12;
inline constexpr uint16_t Rows[Height]={
    0b1111111111111111,
    0b1000000000000001,
    0b1000000000010001,
    0b1000000000010001,
    0b1000010000010001,
    0b1000100000010001,
    0b1001111111110001,
    0b1000100000000001,
    0b1000010000000001,
    0b1000000000000001,
    0b1000000000000001,
    0b1111111111111111,
};
} // namespace geoputer::enter_icon

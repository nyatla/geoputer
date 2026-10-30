#pragma once
#include <cstdint>
namespace geoputer::enter_icon {
// Native square keycap, centered in the 14-pixel footer. Constant ROM rows.
inline constexpr unsigned Width=12, Height=12;
inline constexpr uint16_t Rows[Height]={
    0b111111111111,
    0b100000000001,
    0b100000001001,
    0b100000001001,
    0b100010001001,
    0b100100001001,
    0b101111111001,
    0b100100000001,
    0b100010000001,
    0b100000000001,
    0b100000000001,
    0b111111111111,
};
static_assert(Width==Height,"Enter keycap must remain square");
} // namespace geoputer::enter_icon

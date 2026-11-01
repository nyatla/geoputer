#pragma once
#include <cstdint>
namespace geoputer::alarm_icon {
// Twelve native pixels; immutable pattern lives in Flash/DROM.
inline constexpr unsigned Width=12,Height=12;
inline constexpr uint16_t Rows[Height]={
    0b000001100000, 0b000011110000, 0b000110011000,
    0b001100001100, 0b001000000100, 0b001000000100,
    0b001000000100, 0b011000000110, 0b110000000011,
    0b111111111111, 0b000011110000, 0b000001100000
};
} // namespace geoputer::alarm_icon

#pragma once
#include <cstdint>

namespace geoputer::sd_icon {
inline constexpr int X=166,Y=2,Width=12,Height=12;
inline constexpr int ReservedLeft=162,HeaderRight=158;
// Fixed 1-bit planes in Flash/DROM. Shape plus border, contacts and 3x5 SD label.
inline constexpr uint16_t Rows[Height]={
    0b000111111111,
    0b001111111111,
    0b011111111111,
    0b111111111111,
    0b111111111111,
    0b111111111111,
    0b111111111111,
    0b111111111111,
    0b111111111111,
    0b111111111111,
    0b111111111111,
    0b111111111111,
};
inline constexpr uint16_t InkRows[Height]={
    0b000111111111,
    0b001000000001,
    0b010010101001,
    0b100010101001,
    0b100000000001,
    0b101110110001,
    0b101000101001,
    0b101110101001,
    0b100010101001,
    0b101110110001,
    0b100000000001,
    0b111111111111,
};
} // namespace geoputer::sd_icon

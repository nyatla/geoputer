#pragma once

#include <cstdint>

namespace geoputer::ellipsis_glyph {

// One native cell in each font size. The dots sit on the same baseline.
inline constexpr uint8_t Narrow16[16] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0x54,0x54,0,0
};
inline constexpr uint16_t Wide16[16] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0x1110,0x1110,0,0
};
inline constexpr uint8_t Narrow8[8] = {0,0,0,0,0,0,0x54,0};

} // namespace geoputer::ellipsis_glyph

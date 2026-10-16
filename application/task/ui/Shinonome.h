#pragma once

#include <cstdint>

namespace geoputer {

// Sixteen top-to-bottom rows; bit 15 is the leftmost pixel. Data lives in flash.
// Unsupported characters use Shinonome's own white-square glyph.
const uint16_t* shinonomeGlyph(uint32_t codepoint);

}  // namespace geoputer

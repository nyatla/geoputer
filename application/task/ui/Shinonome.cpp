#include "Shinonome.h"
#include <cstddef>

namespace geoputer {
namespace {
struct Glyph { uint16_t codepoint; uint16_t rows[16]; };
const Glyph Glyphs[] = {
#include "ShinonomeData.inc"
};

const uint16_t* find(uint32_t codepoint) {
    size_t low = 0, high = sizeof(Glyphs) / sizeof(Glyphs[0]);
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (Glyphs[middle].codepoint < codepoint) low = middle + 1;
        else high = middle;
    }
    return low < sizeof(Glyphs) / sizeof(Glyphs[0]) && Glyphs[low].codepoint == codepoint
               ? Glyphs[low].rows : nullptr;
}
}  // namespace

const uint16_t* shinonomeGlyph(uint32_t codepoint) {
    const auto* glyph = find(codepoint);
    return glyph ? glyph : find(0x25A1);
}

}  // namespace geoputer

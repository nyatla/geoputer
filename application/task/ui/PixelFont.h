#pragma once

#include "UiCanvas.h"

namespace geoputer {

enum class TextRole { UI, Place, UiLarge, Ascii8x16 };

// Native Shinonome 16x16, ASCII 8x16 and small 8x8 glyphs, without antialiasing.
class PixelFont {
public:
    explicit PixelFont(M5GFX& display) : glyph_(&display) {}
    bool begin();
    static int width(const char* text, TextRole role);
    void draw(UiCanvas& target, int x, int y, const char* text, TextRole role, uint16_t color);

private:
    // One 8x16 monochrome glyph plus M5GFX's trailing guard byte, aligned.
    alignas(4) uint8_t glyph_buffer_[20]{};
    M5Canvas glyph_;
};

}  // namespace geoputer

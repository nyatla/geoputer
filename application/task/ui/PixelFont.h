#pragma once

#include <M5GFX.h>

namespace geoputer {

enum class TextRole { UI, Place, UiLarge, Ascii8x16 };

// Native Shinonome 16x16, ASCII 8x16 and small 8x8 glyphs, without antialiasing.
class PixelFont {
public:
    explicit PixelFont(M5GFX& display) : glyph_(&display) {}
    bool begin();
    static int width(const char* text, TextRole role);
    void draw(M5Canvas& target, int x, int y, const char* text, TextRole role, uint16_t color);

private:
    M5Canvas glyph_;
};

}  // namespace geoputer

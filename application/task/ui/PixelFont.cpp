#include "PixelFont.h"
#include "Shinonome.h"
#include <cstring>

namespace geoputer {

bool PixelFont::begin() {
    glyph_.setColorDepth(1);
    if (!glyph_.createSprite(8, 16)) return false;
    glyph_.setTextDatum(top_left);
    glyph_.setTextColor(TFT_WHITE, TFT_BLACK);
    return true;
}

int PixelFont::width(const char* text, TextRole role) {
    int result = 0;
    while (*text) {
        const auto first = static_cast<unsigned char>(*text);
        const int length = first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
        for (int i = 0; i < length; ++i)
            if (!text[i] || (i && (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)) return result;
        result += role == TextRole::UI || (role == TextRole::Ascii8x16 && first < 0x80) ? 8 : 16;
        text += length;
    }
    return result;
}

void PixelFont::draw(M5Canvas& target, int x, int y, const char* text,
                     TextRole role, uint16_t color) {
    while (*text) {
        const auto first = static_cast<unsigned char>(*text);
        const bool ascii16 = role == TextRole::Ascii8x16 && first < 0x80;
        const int cell = role == TextRole::UI || ascii16 ? 8 : 16;
        const int height = role == TextRole::UI ? 8 : 16;
        if (x + cell > target.width()) break;
        const int length = first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
        char character[5]{};
        uint32_t codepoint = first & (length == 1 ? 0x7F : length == 2 ? 0x1F : length == 3 ? 0x0F : 0x07);
        bool valid = true;
        for (int i = 0; i < length; ++i) {
            if (!text[i] || (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)) {
                valid = false;
                break;
            }
            character[i] = text[i];
            if (i > 0) codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
        }
        if (!valid) break;

        if (cell == 16) {
            const auto* rows = shinonomeGlyph(codepoint);
            for (int row = 0; row < 16; ++row)
                for (int col = 0; col < 16; ++col)
                    if (rows[row] & (0x8000u >> col)) target.drawPixel(x + col, y + row, color);
            text += length;
            x += cell;
            continue;
        }

        glyph_.fillSprite(TFT_BLACK);
        glyph_.setTextSize(1);
        glyph_.setFont(ascii16 ? static_cast<const lgfx::IFont*>(&fonts::AsciiFont8x16) :
                       first < 0x80 ? static_cast<const lgfx::IFont*>(&fonts::Font8x8C64)
                                   : static_cast<const lgfx::IFont*>(&fonts::lgfxJapanGothic_8));
        glyph_.drawString(character, 0, 0);
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < cell; ++col) {
                if (glyph_.readPixel(col, row)) target.drawPixel(x + col, y + row, color);
            }
        }
        text += length;
        x += cell;
    }
}

}  // namespace geoputer

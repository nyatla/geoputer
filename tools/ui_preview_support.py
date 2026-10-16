"""Host framebuffer support for native-pixel UI documentation images.

Uses the installed M5GFX C64 and ASCII 8x16 fonts. The adapter supports the
integer-pixel operations used by the Live Place and Settings renderers.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "doc/画面・操作/live-place"
M5GFX = ROOT / ".pio/libdeps/cardputer-adv/M5GFX/src/lgfx"

ADAPTER = r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>
#define PROGMEM
#include "Font8x8C64.h"
#include "Ascii8x16.h"
namespace lgfx { struct IFont { bool ascii; int height; }; }
namespace fonts {
inline const lgfx::IFont Font8x8C64{true,8}, AsciiFont8x16{true,16}, lgfxJapanGothic_8{false,8};
}
inline constexpr int top_left = 0;
class M5GFX {
public:
    std::vector<uint16_t> pixels = std::vector<uint16_t>(240 * 135);
    int width() const { return 240; }
    int height() const { return 135; }
    void setRotation(int) {}
    void setBrightness(int) {}
};
class M5Canvas {
    M5GFX* display_;
    int width_ = 0, height_ = 0;
    std::vector<uint16_t> pixels_;
    const lgfx::IFont* font_ = nullptr;
    uint16_t foreground_ = 0xffff, background_ = 0;
public:
    explicit M5Canvas(M5GFX* display) : display_(display) {}
    void setColorDepth(int) {}
    bool createSprite(int width, int height) {
        width_ = width; height_ = height;
        pixels_.assign(width * height, 0);
        return true;
    }
    int width() const { return width_; }
    void setTextDatum(int) {}
    void setTextSize(int size) {
        if (size != 1) throw std::runtime_error("Scaled text unsupported");
    }
    void setTextColor(uint16_t foreground, uint16_t background) {
        foreground_ = foreground; background_ = background;
    }
    void setFont(const lgfx::IFont* font) { font_ = font; }
    void fillSprite(uint16_t color) {
        std::fill(pixels_.begin(), pixels_.end(), color);
    }
    void drawPixel(int x, int y, uint16_t color) {
        if (x >= 0 && y >= 0 && x < width_ && y < height_)
            pixels_[y * width_ + x] = color;
    }
    uint16_t readPixel(int x, int y) const {
        return pixels_.at(y * width_ + x);
    }
    void fillRect(int x, int y, int width, int height, uint16_t color) {
        for (int row = y; row < y + height; ++row)
            for (int col = x; col < x + width; ++col) drawPixel(col, row, color);
    }
    void drawRect(int x, int y, int width, int height, uint16_t color) {
        fillRect(x,y,width,1,color); fillRect(x,y+height-1,width,1,color);
        fillRect(x,y,1,height,color); fillRect(x+width-1,y,1,height,color);
    }
    void drawFastHLine(int x, int y, int width, uint16_t color) {
        fillRect(x, y, width, 1, color);
    }
    void drawString(const char* text, int x, int y) {
        if (!font_ || !font_->ascii)
            throw std::runtime_error("Only ASCII bitmap fonts supported by this adapter");
        for (auto ch = reinterpret_cast<const unsigned char*>(text); *ch; ++ch) {
            if (*ch < 32 || *ch > 127) throw std::runtime_error("Non-ASCII text");
            // M5GFX GLCDfont: eight columns, bit 0 is the top dot.
            for (int col = 0; col < 8; ++col)
                for (int row = 0; row < font_->height; ++row)
                    drawPixel(x + col, y + row,
                        (font_->height == 16 ? FontLib8x16[*ch * 16 + row] & (0x80u >> col) :
                         font8x8_c64[(*ch - 32) * 8 + col] & (1u << row))
                            ? foreground_ : background_);
            x += 8;
        }
    }
    void pushSprite(int x, int y) {
        if (x != 0 || y != 0 || width_ != 240 || height_ != 135)
            throw std::runtime_error("Unexpected framebuffer dimensions");
        display_->pixels = pixels_;
    }
};
'''

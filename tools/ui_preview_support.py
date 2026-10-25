"""Host framebuffer support for native-pixel UI documentation images.

Uses the installed M5GFX C64, ASCII 8x16 and Japanese Gothic 8px fonts.
The adapter supports the
integer-pixel operations used by the Live Place and Settings renderers.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "doc/画面・操作/live-place"
M5GFX = ROOT / ".pio/libdeps/cardputer-adv/M5GFX/src/lgfx"

ADAPTER = r'''
#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>
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
JAPANESE8_HELPER
inline constexpr int top_left = 0;
inline uint8_t previewColorDepth() {
    const auto* depth = std::getenv("GEOPUTER_PREVIEW_DEPTH");
    return depth ? std::atoi(depth) : 4;
}
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
    int depth_ = 16;
    std::vector<uint16_t> palette_;
public:
    explicit M5Canvas(M5GFX* display) : display_(display) {}
    void setColorDepth(int depth) { depth_ = depth; }
    bool createPalette(const uint16_t* colors, uint32_t count) {
        if (depth_ != 4 || count > 16) throw std::runtime_error("invalid palette");
        palette_.assign(16, 0);
        std::copy(colors, colors + count, palette_.begin());
        return true;
    }
    void validColor(uint16_t color) const {
        if (depth_ == 4 && (color >= 16 || palette_.size() != 16))
            throw std::runtime_error("RGB color passed to indexed sprite");
    }
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
        validColor(color);
        std::fill(pixels_.begin(), pixels_.end(), color);
    }
    void drawPixel(int x, int y, uint16_t color) {
        validColor(color);
        if (x >= 0 && y >= 0 && x < width_ && y < height_)
            pixels_[y * width_ + x] = color;
    }
    uint16_t readPixel(int x, int y) const {
        const auto raw = pixels_.at(y * width_ + x);
        return depth_ == 4 ? palette_.at(raw) : raw;
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
        if (!font_) throw std::runtime_error("Font not selected");
        if (!font_->ascii) {
            const auto rows=previewJapanese8(text);
            for(int row=0;row<8;++row)for(int col=0;col<8;++col)
                drawPixel(x+col,y+row,rows[row]&(1u<<col)?foreground_:background_);
            return;
        }
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
        for (int row = 0; row < height_; ++row) for (int col = 0; col < width_; ++col)
            display_->pixels[row * width_ + col] = readPixel(col, row);
    }
};
'''


# Decode the installed M5GFX Japanese 8px font for pixel-accurate host previews.
_font_source = (M5GFX / "Fonts/IPA/lgfx_font_japan.c").read_text()
_font_literals = re.search(
    r'const uint8_t lgfx_font_japan_gothic_8\[\d+\] =\s*((?:"(?:\\.|[^"\\])*"\s*)+);',
    _font_source).group(1)
_JAPANESE8 = r'''
inline const char previewJapan8Data[] = FONT_LITERALS;
inline std::array<uint8_t,8> previewJapanese8(const char* text) {
    const auto* f=reinterpret_cast<const uint8_t*>(previewJapan8Data);
    const auto* t=reinterpret_cast<const uint8_t*>(text);
    unsigned count=*t<0xe0?2:*t<0xf0?3:4,cp=*t&((1u<<(7-count))-1);
    for(unsigned i=1;i<count;++i)cp=(cp<<6)|(t[i]&63);
    auto be=[](const uint8_t* p){return (unsigned(p[0])<<8)|p[1];};
    const uint8_t* glyph=f+23+be(f+21);const uint8_t* lut=glyph;
    unsigned last;
    do {glyph+=be(lut);last=be(lut+2);lut+=4;}while(last<cp);
    while(be(glyph) && be(glyph)!=cp)glyph+=glyph[2];
    std::array<uint8_t,8> rows{};
    if(!be(glyph)) {rows={0x7f,0x41,0x41,0x41,0x41,0x41,0x7f,0};return rows;}
    glyph+=3;unsigned bit=0;
    auto bits=[&](unsigned n) {
        unsigned value=0;
        for(unsigned i=0;i<n;++i) {value|=((*glyph>>bit)&1u)<<i;if(++bit==8){bit=0;++glyph;}}
        return value;
    };
    auto signedBits=[&](unsigned n){return int(bits(n))-(1<<(n-1));};
    const unsigned w=bits(f[4]),h=bits(f[5]);
    const int dx=signedBits(f[6]),gy=signedBits(f[7]);signedBits(f[8]);
    const int dy=int(f[10])+int(int8_t(f[12]))-gy-int(h);
    if(!w)return rows;
    unsigned x=0,y=0;
    do {
        unsigned runs[]={bits(f[2]),bits(f[3])};bool ink=false;
        do {
            for(unsigned left=runs[ink];left;--left) {
                const int px=dx+int(x),py=dy+int(y);
                if(ink && px>=0 && px<8 && py>=0 && py<8)rows[py]|=1u<<px;
                if(++x==w){x=0;++y;}
            }
            ink=!ink;
        }while(ink || bits(1));
    }while(y<h);
    return rows;
}
'''.replace("FONT_LITERALS", _font_literals)
ADAPTER = ADAPTER.replace("JAPANESE8_HELPER", _JAPANESE8)


def reference_frame(executable, *arguments, timeout=20):
    """Compare palette rendering against the production RGB565 reference path."""
    import os
    import subprocess
    result = []
    for depth in (4, 16):
        env = dict(os.environ, GEOPUTER_PREVIEW_DEPTH=str(depth))
        result.append(subprocess.check_output([str(executable), *arguments], env=env, timeout=timeout))
    assert result[0] == result[1], f"Palette/RGB565 pixels differ: {arguments}"
    return result[0]

#pragma once

#include <M5GFX.h>
#include <stdexcept>
#include "UiFrameBuffer.h"

namespace geoputer {

// Renderer-facing RGB565 API. Only the backing sprite uses palette indices.
// Keeping the conversion here also covers individual font pixels.
class UiCanvas : public M5Canvas {
public:
    explicit UiCanvas(M5GFX* display) : M5Canvas(display) {}

    // Include M5GFX's trailing pixel guard. The UI task supplies its own storage;
    // host RGB565 reference renders may still use an internally owned sprite.
    static constexpr size_t bufferBytes(uint8_t depth) {
        return uiFrameBufferBytes(depth);
    }
    bool begin(uint8_t depth, void* buffer = nullptr, size_t capacity = 0) {
        if (depth != 4 && depth != 16) return false;
        if (buffer && capacity < bufferBytes(depth)) return false;
        depth_ = depth;
        setColorDepth(depth);
        if (buffer) setBuffer(buffer, 240, 135, depth);
        else if (!createSprite(240, 135)) return false;
        return depth != 4 || createPalette(Colors, sizeof(Colors) / sizeof(Colors[0]));
    }

    void fillSprite(uint16_t color) { M5Canvas::fillSprite(encoded(color)); }
    void drawPixel(int32_t x, int32_t y, uint16_t color) { M5Canvas::drawPixel(x, y, encoded(color)); }
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
        M5Canvas::fillRect(x, y, w, h, encoded(color));
    }
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
        M5Canvas::drawRect(x, y, w, h, encoded(color));
    }
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color) {
        M5Canvas::drawFastHLine(x, y, w, encoded(color));
    }
    // Dim during base-screen rendering, without another framebuffer or pass.
    void setDimmed(bool dimmed) { dimmed_ = dimmed; }

private:
    uint16_t encoded(uint16_t color) const {
        if (dimmed_ && color != TFT_BLACK) color = TFT_DARKGREY;
        if (depth_ == 16) return color;
        for (uint16_t i = 0; i < sizeof(Colors) / sizeof(Colors[0]); ++i)
            if (Colors[i] == color) return i;
        throw std::invalid_argument("UI color is missing from the palette");
    }
    inline static constexpr uint16_t Colors[] = {
        TFT_BLACK, TFT_WHITE, TFT_DARKGREY, TFT_LIGHTGREY, TFT_CYAN,
        TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW
    };
    uint8_t depth_ = 16;
    bool dimmed_ = false;
};

}  // namespace geoputer

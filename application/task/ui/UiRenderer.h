#pragma once

#include <M5GFX.h>
#include "PixelFont.h"
#include "UiModel.h"

namespace geoputer {

class UiRenderer {
public:
    explicit UiRenderer(M5GFX& display) : display_(display), frame_(&display), font_(display) {}
    bool begin();
    void draw(const UiModel& model);

private:
    void text(int x, int y, const char* value, uint16_t color = TFT_WHITE);
    void largeText(int x, int y, const char* value, uint16_t color = TFT_WHITE);
    void speedText(const char* value);
    M5GFX& display_;
    M5Canvas frame_;
    PixelFont font_;
};

}  // namespace geoputer

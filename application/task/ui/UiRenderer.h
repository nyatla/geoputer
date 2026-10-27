#pragma once

#include <M5GFX.h>
#include "PixelFont.h"
#include "UiModel.h"
#include "../../Config.h"

namespace geoputer {

class UiRenderer {
public:
    explicit UiRenderer(M5GFX& display) : display_(display), frame_(&display), font_(display) {}
    bool begin(uint8_t color_depth = config::UiColorDepth, void* buffer = nullptr, size_t capacity = 0);
    void draw(const UiModel& model);

private:
    void present(const UiModel& model,UiLanguage language);
    void drawHeaderStatus(const UiModel& model);
    void drawMap(const UiModel& model);
    void drawScreenGuide(const UiModel& model);
    void drawKeyArrow(int x, int y, UiKey direction, bool enabled);
    void drawDialogGuide(const ChoiceDialog& dialog);
    void drawBoot(const UiModel& model, const Settings& settings);
    void drawLivePlace(const UiModel& model, const UiSnapshot& content, const Settings& settings);
    void drawLogBrowser(const UiModel& model, const UiSnapshot& content, const Settings& settings);
    void drawGpsLogger(const UiModel& model);
    void drawSettings(const UiModel& model, const UiSnapshot& content, const Settings& settings);

    void text(int x, int y, const char* value, uint16_t color = TFT_WHITE);
    void valueText(int left, int right, int y, const char* value, uint16_t color = TFT_WHITE);
    void largeText(int x, int y, const char* value, uint16_t color = TFT_WHITE);
    void speedText(const char* value);
    void drawDialog(const UiModel& model, UiLanguage language);
    void drawUsb(const UiModel& model);
    M5GFX& display_;
    UiCanvas frame_;
    PixelFont font_;
};

}  // namespace geoputer

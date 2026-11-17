#pragma once

#include <M5GFX.h>
#include "PixelFont.h"
#include "GpsUiModel.h"
#include "../../Config.h"

namespace geoputer {

class GpsUiRenderer {
public:
    explicit GpsUiRenderer(M5GFX& display) : display_(display), frame_(&display), font_(display) {}
    bool begin(uint8_t color_depth = config::UiColorDepth, void* buffer = nullptr, size_t capacity = 0);
    void draw(const GpsUiModel& model);

private:
    static TextRole placeRole(PlaceTextMode mode);
    void present(const GpsUiModel& model,const UiSnapshot& content,UiLanguage language);
    void drawRuntimeHeader(const GpsUiModel& model);
    void drawPlaybackHeader(const GpsUiModel& model);
    void drawScreenName(const GpsUiModel& model);
    void drawHeaderStatus(const GpsUiModel& model);
    void drawMap(const GpsUiModel& model,const UiSnapshot& content);
    void drawScreenGuide(const GpsUiModel& model);
    void drawKeyArrow(int x, int y, UiKey direction, bool enabled);
    void drawBoot(const GpsUiModel& model, const Settings& settings);
    void drawLivePlace(const GpsUiModel& model, const UiSnapshot& content, const Settings& settings);
    void drawLogBrowser(const GpsUiModel& model, const UiSnapshot& content, const Settings& settings);
    void drawGpsLogger(const GpsUiModel& model);
    void drawSettings(const GpsUiModel& model, const UiSnapshot& content, const Settings& settings);
    void drawDatasetSettings(const GpsUiModel& model, const UiSnapshot& content, const Settings& settings);

    void text(int x, int y, const char* value, uint16_t color = TFT_WHITE);
    void valueText(int left, int right, int y, const char* value, uint16_t color = TFT_WHITE);
    void largeText(int x, int y, const char* value, uint16_t color = TFT_WHITE);
    void speedText(const char* value);
    void drawDialog(const GpsUiModel& model,const UiSnapshot& content,UiLanguage language);
    M5GFX& display_;
    UiCanvas frame_;
    PixelFont font_;
};

}  // namespace geoputer

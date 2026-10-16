#include <M5Cardputer.h>
#include <esp_heap_caps.h>
#include <memory>
#include "../../../license_scenarios.h"

// Compile the production renderer/model unchanged in this standalone test image.
// No App, GPS or Storage tasks, SD mounting or NVS writes run in this image.
#include "../../../../application/Settings.cpp"
#include "../../../../application/task/ui/UiModel.cpp"
#include "../../../../application/task/ui/UiRenderer.cpp"
#include "../../../../application/task/ui/SettingsText.cpp"
#include "../../../../application/task/ui/PixelFont.cpp"
#include "../../../../application/task/ui/Shinonome.cpp"
#include "../../../../application/task/ui/LicenseText.cpp"

namespace {
geoputer::UiModel model;
std::unique_ptr<geoputer::UiRenderer> renderer;
uint32_t frames = 0, maximum_us = 0, slow_frames = 0;
uint64_t total_us = 0;

void paint(const geoputer::UiModel& ui) {
    M5Cardputer.update();
    const auto started = micros();
    renderer->draw(ui);
    const uint32_t elapsed = micros() - started;
    total_us += elapsed;
    if (elapsed > maximum_us) maximum_us = elapsed;
    if (elapsed > 10000u) ++slow_frames;
    ++frames;
    if (frames % 64u == 0)
        Serial.printf("[LICENSE-TEST] frames=%lu offset=%lu max_us=%lu heap=%u stack_free=%u\n",
                      static_cast<unsigned long>(frames), static_cast<unsigned long>(ui.licenseOffset()),
                      static_cast<unsigned long>(maximum_us), ESP.getFreeHeap(),
                      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    delay(1);  // This harness must yield between measured frames.
}
}

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    auto config = M5.config();
    config.internal_spk = false;
    config.internal_mic = false;
    M5Cardputer.begin(config, true);
    pinMode(5, OUTPUT); digitalWrite(5, HIGH);
    pinMode(12, OUTPUT); digitalWrite(12, HIGH);
    delay(5000);  // Allow the serial capture to attach after flashing/resetting.
    try {
        renderer.reset(new geoputer::UiRenderer(M5Cardputer.Display));
        geoputer::test::licenseCheck(renderer->begin(), "renderer initialization");
        Serial.printf("[LICENSE-TEST] START lines=%u rows=%u\n",
                      static_cast<unsigned>(geoputer::licenseLineCount()), geoputer::SettingsLayout::LicenseRows);
        geoputer::test::exerciseLicenses(model, paint);
        const auto heap_before = ESP.getFreeHeap();
        for (unsigned i = 0; i < 128; ++i) paint(model);
        Serial.printf("[LICENSE-TEST] PASS frames=%lu average_us=%lu max_us=%lu over_10ms=%lu heap_before=%u heap_after=%u min_heap=%u largest=%u stack_free=%u\n",
                      static_cast<unsigned long>(frames), static_cast<unsigned long>(total_us / frames),
                      static_cast<unsigned long>(maximum_us), static_cast<unsigned long>(slow_frames),
                      heap_before, ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    } catch (const std::exception& error) {
        Serial.printf("[LICENSE-TEST] FAIL %s\n", error.what());
    }
}

void loop() { delay(1000); }

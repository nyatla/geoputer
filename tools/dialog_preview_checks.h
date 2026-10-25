#pragma once

#include "task/ui/UiRenderer.h"
#include <cstring>
#include <stdexcept>

// Host-only pixel contract shared by the real UI preview generators.
inline void renderAndCheckDialog(geoputer::UiRenderer& renderer, geoputer::UiModel& model, M5GFX& display) {
    using namespace geoputer;
    if (!model.confirming()) { renderer.draw(model); return; }
    // UiModel owns noncopyable map buffers. Hide only the optional dialog in
    // this host fixture; restore it even when rendering throws. Cancellation
    // itself is covered by the native input tests.
    auto drawBackground = [&] {
        auto& dialog = const_cast<ChoiceDialog&>(model.dialog());
        const auto saved = dialog;
        dialog.close();
        try { renderer.draw(model); }
        catch (...) { dialog = saved; throw; }
        dialog = saved;
    };
    drawBackground();
    const auto original = display.pixels;
    renderer.draw(model);
    int top = 38, bottom = 102;
    if (model.dialog().kind() == DialogKind::FormatSd || model.dialog().kind() == DialogKind::ResetSettings ||
        model.dialog().kind() == DialogKind::EnterUsbMode || model.dialog().kind() == DialogKind::ExitUsbMode) {
        top = 34; bottom = 106;
    } else if (model.dialog().kind() == DialogKind::DeleteLog &&
               std::strlen(model.data().browse().names[model.snapshot().selected_setting]) > 27) {
        top = 33; bottom = 107;
    }
    for (int x = 8; x < 232; ++x)
        if (display.pixels[top * 240 + x] != TFT_WHITE || display.pixels[bottom * 240 + x] != TFT_WHITE)
            throw std::runtime_error("modal must have white one-pixel horizontal rules");
    for (int y = 0; y < 121; ++y) for (int x = 0; x < 240; ++x) {
        if (x >= 8 && x < 232 && y >= top && y <= bottom) continue;
        const auto expected = original[y * 240 + x] == TFT_BLACK ? TFT_BLACK : TFT_DARKGREY;
        if (display.pixels[y * 240 + x] != expected)
            throw std::runtime_error("modal backdrop must preserve and dim the original screen");
    }
    const auto modal = display.pixels;
    drawBackground();
    if (display.pixels != original) throw std::runtime_error("closing a dialog must restore the base screen colors");
    renderer.draw(model);
    if (display.pixels != modal) throw std::runtime_error("modal redraw must be deterministic");
}

#pragma once

#include <cstring>
#include <stdexcept>
#include "../application/task/ui/UiModel.h"
#include "../application/task/ui/LicenseText.h"
#include "../application/task/ui/SettingsLayout.h"

namespace geoputer::test {
inline void licenseCheck(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// The same state transitions run on the host and against the real LCD. Render
// non-overlapping pages so every document line reaches the physical renderer.
template <typename Paint>
void exerciseLicenses(UiModel& ui, Paint paint) {
    SetUiScreen command{};
    command.screen = ScreenId::Settings;
    command.mode = DisplayMode::View;
    command.generation = 1;
    command.data.revision = 1;
    command.data.settings = defaultSettings();
    command.data.storage.settings_state = SettingsState::Defaults;
    uint32_t now = 1000;
    licenseCheck(ui.setScreen(command) == OperationStatus::Ok, "set Settings");
    auto key = [&](UiKey value) { now += 10; ui.keyPressed(value, now); ui.tick(now); };
    auto open = [&]() {
        for (unsigned i = 0; i < 3; ++i) key(UiKey::Down);
        key(UiKey::Enter);
        licenseCheck(ui.snapshot().page == SettingsPage::Licenses && ui.licenseOffset() == 0, "open licenses at the start");
    };
    open();
    const size_t last = licenseLineCount() - SettingsLayout::LicenseRows;
    UpdateUiContent update{DisplayMode::View, 1, command.data, 0, ActionResult::None};
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (size_t step = 0; step <= last; ++step) {
            const size_t expected = pass == 0 ? step : last - step;
            licenseCheck(ui.licenseOffset() == expected, "scroll offset matches every step");
            if (step % 17 == 0) {
                ++update.data.revision;
                ++update.data.gps.revision;
                update.data.gps.reception = step % 2 ? GpsReception::NoFix : GpsReception::NoData;
                update.data.storage.busy = (step % 3 == 0);
                std::strcpy(update.data.message, "STORAGE TEST");
                update.data.message_at_ms = now;
                licenseCheck(ui.updateContent(update) == OperationStatus::Ok, "accept asynchronous status data");
                licenseCheck(ui.licenseOffset() == expected && ui.snapshot().page == SettingsPage::Licenses,
                             "GPS/storage updates preserve license position");
                auto stale = update;
                --stale.data.revision;
                licenseCheck(ui.updateContent(stale) == OperationStatus::StaleContent, "reject stale content while reading");
            }
            for (const auto ignored : {UiKey::Left, UiKey::Right, UiKey::Enter, UiKey::OpenSettings}) key(ignored);
            licenseCheck(ui.snapshot().action.kind == ActionKind::None && !ui.editing() && !ui.confirming(),
                         "license input cannot issue storage/settings operations");
            ui.notificationSent();  // App consumed the local content-change notice.
            licenseCheck(!ui.notificationPending(), "content change notification settles");
            if (step % SettingsLayout::LicenseRows == 0 || step == last) paint(ui);
            if (step != last) key(pass == 0 ? UiKey::Down : UiKey::Up);
        }
        for (unsigned i = 0; i < 32; ++i) key(pass == 0 ? UiKey::Down : UiKey::Up);
        licenseCheck(ui.licenseOffset() == (pass == 0 ? last : 0), "scroll clamps at both boundaries");
    }
    // Exercise repeated close/reopen, then idle expiry while reading.
    for (unsigned i = 0; i < 20; ++i) {
        key(UiKey::Escape);
        licenseCheck(ui.snapshot().page == SettingsPage::Root && ui.snapshot().action.kind == ActionKind::None,
                     "Back returns to the Settings root locally");
        open();
        key(UiKey::Down);
        paint(ui);
    }
    ui.tick(now + ScreenTimeouts[update.data.settings.screen_timeout] * 1000u - 1u);
    licenseCheck(ui.snapshot().action.kind == ActionKind::None, "reading remains active until idle deadline");
    now += ScreenTimeouts[update.data.settings.screen_timeout] * 1000u;
    ui.tick(now);
    const auto sleep = ui.snapshot().action;
    licenseCheck(sleep.kind == ActionKind::Sleep, "reading follows the configured screen timeout");
    update.mode = DisplayMode::Background;
    update.processed_action_id = sleep.action_id;
    update.action_result = ActionResult::Applied;
    licenseCheck(ui.updateContent(update) == OperationStatus::Ok, "acknowledge screen sleep");
    key(UiKey::Other);
    const auto wake = ui.snapshot().action;
    licenseCheck(wake.kind == ActionKind::Wake, "first key requests wake while in licenses");
    command.screen = ScreenId::LivePlace;
    command.generation = 2;
    command.data = update.data;
    command.processed_action_id = wake.action_id;
    command.action_result = ActionResult::Applied;
    licenseCheck(ui.setScreen(command) == OperationStatus::Ok, "wake returns to Live Place");
    key(UiKey::OpenSettings);
    const auto settings = ui.snapshot().action;
    licenseCheck(settings.kind == ActionKind::ChangeScreen, "2 works after waking from licenses");
    command.screen = ScreenId::Settings;
    command.generation = 3;
    command.processed_action_id = settings.action_id;
    licenseCheck(ui.setScreen(command) == OperationStatus::Ok, "return to Settings after wake");
    open();
    paint(ui);
}
}  // namespace geoputer::test

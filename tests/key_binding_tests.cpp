#include <stdexcept>
#include "task/ui/KeyBindings.h"
#include "task/ui/UiModel.h"

using namespace geoputer;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}

void testKeyBindings() {
    for (bool fn : {false, true}) {
        check(mapPhysicalKey('2', fn) == UiKey::OpenSettings && mapPhysicalKey('3', fn) == UiKey::OpenLogs,
              "physical 2 and 3 open Settings and Logs with or without Fn");
        check(mapPhysicalKey('d', fn) == UiKey::DeleteLog, "physical D is separate from the Del/Back key");
        check(mapPhysicalKey(';', fn) == UiKey::Up && mapPhysicalKey('.', fn) == UiKey::Down &&
              mapPhysicalKey(',', fn) == UiKey::Left && mapPhysicalKey('/', fn) == UiKey::Right,
              "physical punctuation navigation is independent of Fn");
    }
    check(mapPhysicalKey('1', false) == UiKey::OpenGpsLogger && mapPhysicalKey('1', true) == UiKey::F1,
          "plain 1 opens the logger; only Fn+1 pauses logging");
    check(mapPhysicalKey('`', false) == UiKey::Other && mapPhysicalKey('`', true) == UiKey::Escape,
          "Escape retains its Fn modifier");
    check(mapPhysicalKey('a', false) == UiKey::Other, "ordinary keys remain unrelated to navigation");

    UiModel model;
    UiData data{}; data.settings = defaultSettings();
    model.setScreen({ScreenId::LivePlace, DisplayMode::View, 1, data, 0, ActionResult::None});
    model.keyPressed(mapPhysicalKey('2', false), 10);
    auto action = model.snapshot().action;
    check(action.kind == ActionKind::ChangeScreen && action.target == ScreenId::Settings, "plain 2 requests Settings directly through App");
    model.setScreen({ScreenId::Settings, DisplayMode::View, 2, data, action.action_id, ActionResult::Applied});
    model.keyPressed(UiKey::Enter, 60);
    check(model.snapshot().page == SettingsPage::Operation, "settings root opens operation settings");
    model.keyPressed(mapPhysicalKey('/', false), 70);
    check(!model.editing() && model.snapshot().selected_setting == 0, "right cannot navigate an unedited settings list");
    model.keyPressed(UiKey::Enter, 80);
    model.keyPressed(mapPhysicalKey('/', false), 90);
    check(model.editing() && model.editValue() == 1, "plain slash changes the edited language value");
    model.keyPressed(mapPhysicalKey(',', false), 100);
    check(model.editValue() == 0, "plain comma reverses the edited value");
    model.keyPressed(UiKey::Escape, 110);
    model.keyPressed(UiKey::Escape, 120);
    model.keyPressed(mapPhysicalKey('2', false), 130);
    check(model.snapshot().page == SettingsPage::Root && model.snapshot().action.kind == ActionKind::None,
          "shortcut key does not navigate away from Settings");

    UiModel sleeping;
    sleeping.setScreen({ScreenId::LivePlace, DisplayMode::Background, 1, data, 0, ActionResult::None});
    sleeping.keyPressed(mapPhysicalKey('2', false), 200);
    action = sleeping.snapshot().action;
    check(action.kind == ActionKind::Wake, "first plain 2 press while asleep only wakes the display");
    sleeping.updateContent({DisplayMode::View, 1, data, action.action_id, ActionResult::Applied});
    check(sleeping.snapshot().action.kind == ActionKind::None, "wake acknowledgement does not also open Settings");
    sleeping.keyPressed(mapPhysicalKey('2', false), 210);
    check(sleeping.snapshot().action.target == ScreenId::Settings, "next plain 2 press opens Settings after waking");

    for (const char key : {'1', '2', '3'}) {
        UiModel direct;
        data.recording.state = RecordingState::Recording;
        direct.setScreen({ScreenId::LivePlace, DisplayMode::View, 1, data, 0, ActionResult::None});
        direct.keyPressed(mapPhysicalKey(key, false), 10);
        const auto move = direct.snapshot().action;
        const auto target = key=='1'?ScreenId::GpsLogger:key=='2'?ScreenId::Settings:ScreenId::Logs;
        check(move.kind == ActionKind::ChangeScreen && move.target == target && !direct.confirming(),
              "each plain number requests its screen without changing recording");
        check(direct.data().recording.state == RecordingState::Recording, "shortcuts preserve the recording state");
        direct.setScreen({target, DisplayMode::View, 2, data, move.action_id, ActionResult::Applied});
        for (const char ignored : {'1', '2', '3'}) direct.keyPressed(mapPhysicalKey(ignored, false), 20);
        check(direct.snapshot().action.kind == ActionKind::None, "screen shortcuts only apply on Live Place");
        direct.keyPressed(UiKey::Escape, 30);
        check(direct.snapshot().action.target == ScreenId::LivePlace, "all three entry screens return to Live Place");
    }
    UiModel pause;
    pause.setScreen({ScreenId::LivePlace, DisplayMode::View, 1, data, 0, ActionResult::None});
    pause.keyPressed(mapPhysicalKey('1', true), 10);
    check(pause.confirming() && pause.snapshot().action.kind == ActionKind::None, "Fn+1 retains pause confirmation");
    for (const char key : {'1', '2', '3'}) pause.keyPressed(mapPhysicalKey(key, false), 20);
    check(pause.confirming() && pause.snapshot().action.kind == ActionKind::None, "shortcuts cannot bypass pause confirmation");
    for (const bool fn : {false, true}) {
        pause.keyPressed(mapPhysicalKey('/', fn), 21);
        check(pause.confirmChoice(), "slash selects the right dialog choice with or without Fn");
        pause.keyPressed(mapPhysicalKey(',', fn), 22);
        check(!pause.confirmChoice(), "comma selects Cancel with or without Fn");
        for (const char key : {';', '.'}) pause.keyPressed(mapPhysicalKey(key, fn), 23);
        check(!pause.confirmChoice(), "vertical physical keys do not change horizontal dialog choices");
    }
}

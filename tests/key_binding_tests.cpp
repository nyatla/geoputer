#include <stdexcept>
#include "task/ui/KeyBindings.h"
#include "task/ui/UiModel.h"

using namespace geoputer;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}

void testKeyBindings() {
    for (bool fn : {false, true}) {
        check(mapPhysicalKey('2', fn) == UiKey::Menu, "physical 2 opens MENU with or without Fn");
        check(mapPhysicalKey(';', fn) == UiKey::Up && mapPhysicalKey('.', fn) == UiKey::Down &&
              mapPhysicalKey(',', fn) == UiKey::Left && mapPhysicalKey('/', fn) == UiKey::Right,
              "physical punctuation navigation is independent of Fn");
    }
    check(mapPhysicalKey('1', false) == UiKey::Other && mapPhysicalKey('1', true) == UiKey::F1,
          "plain 1 does not pause logging");
    check(mapPhysicalKey('`', false) == UiKey::Other && mapPhysicalKey('`', true) == UiKey::Escape,
          "Escape retains its Fn modifier");
    check(mapPhysicalKey('a', false) == UiKey::Other, "ordinary keys remain unrelated to navigation");

    UiModel model;
    UiData data{}; data.settings = defaultSettings();
    model.setScreen({ScreenId::LivePlace, DisplayMode::View, 1, data, 0, ActionResult::None});
    model.keyPressed(mapPhysicalKey('2', false), 10);
    auto action = model.snapshot().action;
    check(action.kind == ActionKind::ChangeScreen && action.target == ScreenId::Menu, "plain 2 requests MENU through App");
    model.setScreen({ScreenId::Menu, DisplayMode::View, 2, data, action.action_id, ActionResult::Applied});
    model.keyPressed(mapPhysicalKey('.', false), 20);
    check(model.snapshot().selected_setting == 1, "plain period selects SETTINGS in MENU");
    model.keyPressed(mapPhysicalKey(';', false), 30);
    check(model.snapshot().selected_setting == 0, "plain semicolon selects the previous MENU entry");
    model.keyPressed(mapPhysicalKey('.', false), 40);
    model.keyPressed(UiKey::Enter, 50);
    action = model.snapshot().action;
    check(action.target == ScreenId::Settings, "punctuation navigation and Enter open SETTINGS");
    model.setScreen({ScreenId::Settings, DisplayMode::View, 3, data, action.action_id, ActionResult::Applied});
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
          "MENU key does not navigate away from SETTINGS");

    UiModel sleeping;
    sleeping.setScreen({ScreenId::LivePlace, DisplayMode::Background, 1, data, 0, ActionResult::None});
    sleeping.keyPressed(mapPhysicalKey('2', false), 200);
    action = sleeping.snapshot().action;
    check(action.kind == ActionKind::Wake, "first plain 2 press while asleep only wakes the display");
    sleeping.updateContent({DisplayMode::View, 1, data, action.action_id, ActionResult::Applied});
    check(sleeping.snapshot().action.kind == ActionKind::None, "wake acknowledgement does not also open MENU");
    sleeping.keyPressed(mapPhysicalKey('2', false), 210);
    check(sleeping.snapshot().action.target == ScreenId::Menu, "next plain 2 press opens MENU after waking");
}

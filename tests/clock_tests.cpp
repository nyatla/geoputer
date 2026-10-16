#include <cstring>
#include <stdexcept>
#include "task/ui/UiModel.h"

using namespace geoputer;
namespace {
void check(bool value, const char* detail) { if (!value) throw std::runtime_error(detail); }
void text(const DisplayClock& clock, const char* expected) {
    char value[9];
    clock.format(value, sizeof(value));
    check(std::strcmp(value, expected) == 0, "clock displays expected JST time");
}
}

void testClock() {
    DisplayClock clock;
    clock.tick(1000);
    text(clock, "--:--:--");
    check(!clock.setInitialUtc(DisplayClock::DayMs, 1000), "invalid UTC cannot start the clock");
    check(clock.setInitialUtc(0, 1000), "UTC midnight is a valid initial time");
    text(clock, "09:00:00");
    clock.tick(1999); text(clock, "09:00:00");
    clock.tick(2000); text(clock, "09:00:01");
    check(!clock.setInitialUtc(50000, 2000), "later GPS samples do not reset the initial-clock policy");
    text(clock, "09:00:01");

    DisplayClock midnight;
    midnight.setInitialUtc(15u * 3600000u - 500u, 1000);
    text(midnight, "23:59:59");
    midnight.tick(1500); text(midnight, "00:00:00");
    midnight.tick(1500 + DisplayClock::DayMs); text(midnight, "00:00:00");

    DisplayClock wrapped;
    wrapped.setInitialUtc(DisplayClock::DayMs - 500, 0xFFFFFF00u);
    wrapped.tick(244); text(wrapped, "09:00:00");
    wrapped.tick(1244); text(wrapped, "09:00:01");

    UiModel ui;
    ui.tick(1000);
    SetUiScreen command{};
    command.screen = ScreenId::LivePlace;
    command.mode = DisplayMode::View;
    command.generation = 1;
    command.data.settings = defaultSettings();
    command.data.gps.fix.utc_ms = 12345;
    ui.setScreen(command);
    check(!ui.clock().known(), "invalid fix does not supply a trusted initial clock");
    UpdateUiContent update{DisplayMode::View, 1, command.data, 0, ActionResult::None};
    update.data.gps.fix = {35, 140, 10000, true};
    ui.updateContent(update);
    text(ui.clock(), "09:00:10");
    ui.takeRedraw();
    ui.tick(1999);
    check(!ui.takeRedraw(), "clock does not redraw between second boundaries");
    ui.tick(2000);
    check(ui.takeRedraw() && !ui.notificationPending(), "clock redraw does not generate user-action notifications");
    text(ui.clock(), "09:00:11");
    update.data.gps.fix = {};
    ui.updateContent(update);
    ui.tick(3000); text(ui.clock(), "09:00:12");

    command.screen = ScreenId::Settings;
    command.generation = 2;
    ui.setScreen(command);
    ui.takeRedraw();
    ui.tick(4000);
    check(!ui.takeRedraw(), "hidden clock does not trigger Settings redraws");
    command.screen = ScreenId::LivePlace;
    command.mode = DisplayMode::Background;
    command.generation = 3;
    ui.setScreen(command);
    ui.takeRedraw();
    ui.tick(5000);
    check(!ui.takeRedraw(), "sleeping screen does not redraw every clock second");
    command.mode = DisplayMode::View;
    command.generation = 4;
    ui.setScreen(command);
    text(ui.clock(), "09:00:14");
}

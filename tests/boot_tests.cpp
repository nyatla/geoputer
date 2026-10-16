#include <cstring>
#include <stdexcept>
#include "BootStatus.h"
#include "task/ui/UiModel.h"

using namespace geoputer;
namespace {
void check(bool ok, const char* detail) { if (!ok) throw std::runtime_error(detail); }
BootStatus ready() {
    BootStatus result{};
    result.set(BootItem::Display, BootCheckState::Passed, "240X135");
    result.set(BootItem::Tasks, BootCheckState::Passed, "4T/5Q");
    result.set(BootItem::GPS, BootCheckState::Passed, "UART OK");
    result.settings(SettingsState::Loaded);
    result.card(CardState::Mounted);
    result.dataset(PlaceState::Ready);
    return result;
}
}

void testBoot() {
    BootStatus progress{};
    check(!progress.finish(), "pending checks cannot report Ready");
    progress = ready();
    progress.set(BootItem::Dataset, BootCheckState::Checking, "OPENING");
    check(!progress.finish(), "dataset validation must return before Ready");
    progress.dataset(PlaceState::Ready);
    check(progress.finish() && !progress.hasIssues() && progress.holdMs() == 2000, "normal startup finishes without waiting for GPS fix");
    progress.settings(SettingsState::Defaults);
    check(!progress.hasIssues(), "first boot default settings are not an error");
    progress.card(CardState::NoCard);
    progress.dataset(PlaceState::NoCard);
    check(progress.finish() && progress.hasIssues() && progress.holdMs() == 5000, "no SD warns and allows startup");
    progress.settings(SettingsState::Loaded);
    check(progress.checks[static_cast<unsigned>(BootItem::Settings)].state == BootCheckState::Passed,
          "SD failure does not overwrite valid NVS outcome");
    progress = ready();
    progress.settings(SettingsState::Invalid);
    check(progress.finish() && progress.hasIssues() &&
          progress.checks[static_cast<unsigned>(BootItem::SD)].state == BootCheckState::Passed,
          "invalid NVS does not overwrite valid SD outcome");
    for (const auto state : {PlaceState::DatasetMissing, PlaceState::AmbiguousFiles, PlaceState::EncodingError, PlaceState::DataError,
                             PlaceState::TooManyChunks, PlaceState::OutOfMemory}) {
        progress = ready(); progress.dataset(state);
        check(progress.finish() && progress.hasIssues(), "dataset errors are visible and nonfatal");
    }

    UiModel ui;
    SetUiScreen command{};
    command.screen = ScreenId::Boot;
    command.mode = DisplayMode::View;
    command.generation = 1;
    command.data.revision = 1;
    command.data.settings = defaultSettings();
    check(ui.setScreen(command) == OperationStatus::Ok, "Boot is a valid screen");
    ui.tick(100); ui.tick(100000);
    for (auto key : {UiKey::Enter, UiKey::Escape, UiKey::Menu, UiKey::Up, UiKey::Down, UiKey::Left, UiKey::Right}) ui.keyPressed(key, 100001);
    check(ui.snapshot().action.kind == ActionKind::None, "unfinished boot neither sleeps nor accepts screen/menu actions");
    UpdateUiContent update{};
    update.generation = 1; update.mode = DisplayMode::View; update.data = command.data;
    update.data.revision = 2; update.data.boot = ready(); update.data.boot.finish();
    check(ui.updateContent(update) == OperationStatus::Ok, "App can publish completed startup checks");
    ui.keyPressed(UiKey::Menu, 100002);
    check(ui.snapshot().action.kind == ActionKind::None, "2 cannot bypass boot acknowledgement");
    ui.keyPressed(UiKey::Enter, 100003);
    const auto action = ui.snapshot().action;
    check(action.kind == ActionKind::ChangeScreen && action.target == ScreenId::LivePlace, "Enter requests Live Place only after completion");
    command.screen = ScreenId::LivePlace; command.generation = 2;
    command.data = update.data; command.processed_action_id = action.action_id;
    command.action_result = ActionResult::Applied;
    check(ui.setScreen(command) == OperationStatus::Ok, "App acknowledges boot completion and sets Live Place");
    ui.tick(200000);
    check(ui.snapshot().action.kind == ActionKind::None, "long startup cannot immediately put Live Place to sleep");
    ui.tick(215000);
    check(ui.snapshot().action.kind == ActionKind::Sleep, "idle timeout starts after leaving Boot");
}

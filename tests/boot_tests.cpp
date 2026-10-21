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
    check(progress.finish() && progress.hasIssues() && progress.holdMs() == 5000, "no SD warns and completes startup");
    progress.settings(SettingsState::Loaded);
    check(progress.checks[static_cast<unsigned>(BootItem::Settings)].state == BootCheckState::Passed,
          "SD failure does not overwrite valid NVS outcome");
    progress = ready();
    progress.settings(SettingsState::Invalid);
    check(progress.finish() && progress.hasIssues() &&
          progress.checks[static_cast<unsigned>(BootItem::SD)].state == BootCheckState::Passed,
          "invalid NVS does not overwrite valid SD outcome");
    for (const auto state : {PlaceState::DatasetMissing, PlaceState::AmbiguousFiles, PlaceState::EncodingError, PlaceState::DataError,
                             PlaceState::TooManyChunks, PlaceState::OutOfMemory, PlaceState::MetadataError, PlaceState::VersionError, PlaceState::NoCard}) {
        progress = ready(); progress.dataset(state);
        check(progress.finish() && progress.hasIssues(), "dataset errors are visible and nonfatal");
        UiModel degraded;UiData data{};data.settings=defaultSettings();data.boot=progress;
        degraded.setScreen({ScreenId::Boot,DisplayMode::View,1,data,0,ActionResult::None});
        degraded.keyPressed(UiKey::Enter,1);
        check(degraded.snapshot().action.kind==ActionKind::ChangeScreen && degraded.snapshot().action.target==ScreenId::LivePlace,
              "every dataset failure, including unavailable SD, permits boot acknowledgement");
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
    for (auto key : {UiKey::Enter, UiKey::Escape, UiKey::OpenSettings, UiKey::Up, UiKey::Down, UiKey::Left, UiKey::Right}) ui.keyPressed(key, 100001);
    check(ui.snapshot().action.kind == ActionKind::None, "unfinished boot neither sleeps nor accepts screen/menu actions");
    UpdateUiContent update{};
    update.generation = 1; update.mode = DisplayMode::View; update.data = command.data;
    update.data.revision = 2; update.data.boot = ready(); update.data.boot.finish();
    check(ui.updateContent(update) == OperationStatus::Ok, "App can publish completed startup checks");
    ui.keyPressed(UiKey::OpenSettings, 100002);
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
    // A held F1 can precede the first App screen, and must survive that setup.
    UiModel held;
    held.keyPressed(UiKey::F1, 1);
    command = {}; command.screen=ScreenId::Boot;command.mode=DisplayMode::View;
    command.generation=1;command.data.settings=defaultSettings();
    check(held.setScreen(command)==OperationStatus::Ok, "early F1 does not block the initial screen");
    check(held.snapshot().action.kind==ActionKind::Maintenance && held.notificationPending(),
          "F1 held before Boot is retained and notifies App");
    for (const bool fatal : {false,true}) {
        UiModel recovery;
        command.data.boot={};command.data.boot.complete=false;
        if(fatal) command.data.boot.dataset(PlaceState::DatasetMissing);
        check(recovery.setScreen(command)==OperationStatus::Ok,"boot/error screen setup");
        recovery.keyPressed(UiKey::Enter,2);
        check(recovery.snapshot().action.kind==ActionKind::None,"unfinished/fatal Boot cannot enter Live Place");
        recovery.keyPressed(UiKey::F1,3);
        const auto entry=recovery.snapshot().action;
        check(entry.kind==ActionKind::Maintenance && entry.target==ScreenId::Settings,
              "F1 requests maintenance during checks and fatal startup errors");
        command.screen=ScreenId::Settings;command.generation=2;
        command.data.boot.maintenance=true;
        command.data.dataset={PlaceState::TooManyChunks,2000,1500};
        command.processed_action_id=entry.action_id;command.action_result=ActionResult::Applied;
        check(recovery.setScreen(command)==OperationStatus::Ok,
              "maintenance menus remain accessible even with an unusable dataset");
        recovery.tick(500000);recovery.tick(600000);
        check(recovery.snapshot().action.kind==ActionKind::None,"maintenance never sleeps or enters Live Place automatically");
        recovery.keyPressed(UiKey::Enter,600001);
        check(recovery.snapshot().page==SettingsPage::Operation,"maintenance reuses operation settings");
        recovery.keyPressed(UiKey::Escape,600002);
        recovery.keyPressed(UiKey::Escape,600003);
        check(recovery.dialog().kind()==DialogKind::RestartLogger && !recovery.confirmChoice(),
              "maintenance exit opens a Cancel-default restart confirmation");
        recovery.keyPressed(UiKey::Enter,600004);
        check(!recovery.confirming() && recovery.snapshot().action.kind==ActionKind::None,"cancel keeps maintenance settings open");
        recovery.keyPressed(UiKey::Escape,600005);recovery.keyPressed(UiKey::Right,600006);recovery.keyPressed(UiKey::Enter,600007);
        check(recovery.snapshot().action.kind==ActionKind::RestartLogger && recovery.snapshot().action.confirmed,
              "confirmed maintenance exit requests reboot instead of entering the faulty dataset");
        command.screen=ScreenId::Boot;command.generation=1;command.processed_action_id=0;command.action_result=ActionResult::None;
        command.data={};command.data.settings=defaultSettings();
    }
    UiModel usb;
    usb.keyPressed(UiKey::F1,1);
    command.screen=ScreenId::UsbStorage;
    check(usb.setScreen(command)==OperationStatus::Ok && usb.snapshot().action.kind==ActionKind::None,
          "early F1 does not override USB storage boot");
    for (const bool recording : {false,true}) {
        UiModel runtime;
        command.screen=ScreenId::LivePlace;
        command.data.storage.card_state=CardState::NoCard;
        command.data.recording.state=recording?RecordingState::Recording:RecordingState::Stopped;
        runtime.setScreen(command);runtime.keyPressed(UiKey::F1,10);
        check(runtime.snapshot().action.kind==(recording?ActionKind::None:ActionKind::Maintenance),
              "runtime errors allow F1 recovery only after recording is stopped");
    }
    UiModel healthy;
    command.data.storage.card_state=CardState::Mounted;command.data.recording.state=RecordingState::Stopped;
    command.data.gps.fix.valid=true;command.data.gps.reception=GpsReception::Fix;
    healthy.setScreen(command);healthy.keyPressed(UiKey::F1,20);
    check(healthy.snapshot().action.kind==ActionKind::None,"healthy Live Place keeps F1 unassigned");

}

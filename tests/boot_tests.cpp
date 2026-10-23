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
    progress.dataset(PlaceState::Unselected);
    check(progress.finish() && !progress.hasIssues() && progress.holdMs()==2000 &&
          !std::strcmp(progress.checks[static_cast<unsigned>(BootItem::Dataset)].detail,"NONE"),
          "unselected is a normal completed boot check, without warning delay");
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
    for (auto key : {UiKey::Enter, UiKey::Back, UiKey::F1, UiKey::OpenSettings, UiKey::Up, UiKey::Down, UiKey::Left, UiKey::Right}) ui.keyPressed(key, 100001);
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
    // A held ESC can precede the first App screen, and must survive that setup.
    UiModel held;
    held.keyPressed(UiKey::Escape, 1);
    command = {}; command.screen=ScreenId::Boot;command.mode=DisplayMode::View;
    command.generation=1;command.data.settings=defaultSettings();
    check(held.setScreen(command)==OperationStatus::Ok, "early ESC does not block the initial screen");
    check(held.snapshot().action.kind==ActionKind::Maintenance && held.notificationPending(),
          "ESC held before Boot is retained and notifies App");
    const auto held_action=held.snapshot().action;
    command.screen=ScreenId::Settings;command.generation=2;command.data.boot.maintenance=true;
    command.data.storage.busy=true; // Settings have not been read yet.
    command.processed_action_id=held_action.action_id;command.action_result=ActionResult::Applied;
    check(held.setScreen(command)==OperationStatus::Ok && !held.confirming(),
          "startup ESC can show maintenance while NVS/SD initialization is pending");
    held.keyPressed(UiKey::Enter,2);held.keyPressed(UiKey::Enter,3);
    check(!held.editing() && held.snapshot().action.kind==ActionKind::None,
          "settings cannot be saved before startup NVS restoration completes");
    command={};command.screen=ScreenId::Boot;command.mode=DisplayMode::View;command.generation=1;
    command.data.settings=defaultSettings();
    for(const auto key:{UiKey::F1,UiKey::Back}) {
        UiModel unrelated;unrelated.keyPressed(key,1);unrelated.setScreen(command);unrelated.keyPressed(key,2);
        check(unrelated.snapshot().action.kind==ActionKind::None,"F1 and Del cannot select maintenance at boot");
    }
    for (const bool fatal : {false,true}) {
        UiModel recovery;
        command.data.boot={};command.data.boot.complete=false;
        if(fatal) command.data.boot.dataset(PlaceState::DatasetMissing);
        check(recovery.setScreen(command)==OperationStatus::Ok,"boot/error screen setup");
        recovery.keyPressed(UiKey::Enter,2);
        check(recovery.snapshot().action.kind==ActionKind::None,"unfinished/fatal Boot cannot enter Live Place");
        recovery.keyPressed(UiKey::Escape,3);
        const auto entry=recovery.snapshot().action;
        check(entry.kind==ActionKind::Maintenance && entry.target==ScreenId::Settings,
              "ESC requests maintenance during checks and fatal startup errors");
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
    usb.keyPressed(UiKey::Escape,1);
    command.screen=ScreenId::UsbStorage;
    check(usb.setScreen(command)==OperationStatus::Ok && usb.snapshot().action.kind==ActionKind::None,
          "early ESC does not override USB storage boot");
    for (const bool recording : {false,true}) {
        UiModel runtime;
        command.screen=ScreenId::LivePlace;
        command.data.storage.card_state=CardState::NoCard;
        command.data.recording.state=recording?RecordingState::Recording:RecordingState::Stopped;
        runtime.setScreen(command);runtime.keyPressed(UiKey::Back,8);runtime.keyPressed(UiKey::F1,9);
        check(runtime.snapshot().action.kind==ActionKind::None,"runtime Del/F1 do not enter maintenance");
        runtime.keyPressed(UiKey::Escape,10);
        check(runtime.snapshot().action.kind==(recording?ActionKind::ChangeScreen:ActionKind::Maintenance),
              "runtime errors use maintenance when stopped and normal settings while recording");
    }
    UiModel healthy;
    command.data.storage.card_state=CardState::Mounted;command.data.recording.state=RecordingState::Stopped;
    command.data.gps.fix.valid=true;command.data.gps.reception=GpsReception::Fix;
    healthy.setScreen(command);healthy.keyPressed(UiKey::Escape,20);
    check(healthy.snapshot().action.kind==ActionKind::ChangeScreen && healthy.snapshot().action.target==ScreenId::Settings,"healthy Live Place opens Settings with ESC");

}

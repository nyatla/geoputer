#include <stdexcept>
#include "task/ui/KeyBindings.h"
#include "task/ui/UiModel.h"
#include "task/ui/ScreenNavigation.h"

using namespace geoputer;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}

void testKeyBindings() {
    for (bool fn : {false, true}) {
        check(mapPhysicalKey('p', fn) == UiKey::PauseLog && mapPhysicalKey('0', fn) == UiKey::Other,
              "physical P pauses independently of Fn; 0 is unassigned");
        check(mapPhysicalKey('2', fn) == UiKey::OpenGpsLogger && mapPhysicalKey('3', fn) == UiKey::OpenLogs,
              "physical 2 and 3 open GPS and Logs with or without Fn");
        check(mapPhysicalKey('d', fn) == UiKey::DeleteLog, "physical D is separate from the Del/Back key");
        check(mapPhysicalKey(';', fn) == UiKey::Up && mapPhysicalKey('.', fn) == UiKey::Down &&
              mapPhysicalKey(',', fn) == UiKey::Left && mapPhysicalKey('/', fn) == UiKey::Right,
              "physical punctuation navigation is independent of Fn");
    }
    for (bool fn : {false, true}) {
        check(mapLicenseKey(',', fn, true) == UiKey::Left && mapLicenseKey('.', fn, true) == UiKey::Right,
              "printed angle bracket keys jump software notices with or without Fn");
        check(mapLicenseKey('<', fn, false) == UiKey::Left && mapLicenseKey('>', fn, false) == UiKey::Right,
              "literal angle brackets map to software navigation");
        check(mapLicenseKey('.', fn, false) == UiKey::Down && mapLicenseKey(';', fn, true) == UiKey::Up &&
              mapLicenseKey('/', fn, false) == UiKey::Right,
              "license aliases retain ordinary directional scrolling");
    }
    check(mapPhysicalKey('1', false) == UiKey::OpenLivePlace && mapPhysicalKey('1', true) == UiKey::F1,
          "plain 1 opens Live Place; Fn+1 remains a separate unassigned input");
    check(mapPhysicalKey('`', false) == UiKey::Escape && mapPhysicalKey('`', true) == UiKey::Escape,
          "physical backtick is Escape with or without Fn");
    check(mapPhysicalKey('a', false) == UiKey::Other, "ordinary keys remain unrelated to navigation");

    UiModel model;
    UiData data{}; data.settings = defaultSettings();
    data.storage.card_state=CardState::Mounted;
    data.gps.fix.valid=true; data.gps.reception=GpsReception::Fix;
    model.setScreen({ScreenId::LivePlace, DisplayMode::View, 1, data, 0, ActionResult::None});
    model.keyPressed(mapPhysicalKey('`', false), 10);
    auto action = model.snapshot().action;
    check(action.kind == ActionKind::ChangeScreen && action.target == ScreenId::Settings, "plain Esc requests Settings directly through App");
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
    model.keyPressed(UiKey::Back, 110);
    check(!model.editing(), "Del still cancels settings edits");
    model.keyPressed(UiKey::Back, 120);
    model.keyPressed(mapPhysicalKey('`', false), 130);
    check(model.snapshot().page == SettingsPage::Root && model.snapshot().action.kind == ActionKind::None,
          "shortcut key does not navigate away from Settings");

    UiModel sleeping;
    sleeping.setScreen({ScreenId::LivePlace, DisplayMode::Background, 1, data, 0, ActionResult::None});
    sleeping.keyPressed(mapPhysicalKey('`', false), 200);
    action = sleeping.snapshot().action;
    check(action.kind == ActionKind::Wake, "first plain Esc press while asleep only wakes the display");
    sleeping.updateContent({DisplayMode::View, 1, data, action.action_id, ActionResult::Applied});
    check(sleeping.snapshot().action.kind == ActionKind::None, "wake acknowledgement does not also open Settings");
    sleeping.keyPressed(mapPhysicalKey('`', false), 210);
    check(sleeping.snapshot().action.target == ScreenId::Settings, "next plain Esc press opens Settings after waking");

    data.storage.card_state=CardState::Mounted;
    data.gps.fix.valid=true;data.gps.reception=GpsReception::Fix;
    data.recording.state=RecordingState::Recording;
    const ScreenId roots[]{ScreenId::LivePlace,ScreenId::GpsLogger,ScreenId::Logs,ScreenId::Settings};
    const char shortcuts[]{'1','2','3','`'};
    for(const auto from:roots) for(unsigned n=0;n<4;++n) for(bool fn:{false,true}) {
        if(n==0 && fn) continue; // Fn+1 remains the unrelated F1 key.
        UiModel direct;
        direct.setScreen({from,DisplayMode::View,1,data,0,ActionResult::None});
        direct.keyPressed(mapPhysicalKey(shortcuts[n],fn),10);
        const auto move=direct.snapshot().action;
        if(from==roots[n]) check(move.kind==ActionKind::None,"current-screen shortcut is a no-op");
        else {
            check(move.kind==ActionKind::ChangeScreen && move.target==roots[n],"root shortcuts jump directly to their target");
            check(rootScreenTransition(from,direct.snapshot().page,move.target),"App accepts every direct root transition");
            check(direct.setScreen({move.target,DisplayMode::View,2,data,move.action_id,ActionResult::Applied})==OperationStatus::Ok,
                  "one acknowledged screen update reaches the target without Live Place detour");
        }
        check(direct.data().recording.state==RecordingState::Recording,"navigation never changes recording");
    }
    for(const auto child:{ScreenId::LogDetail,ScreenId::LogPoint}) {
        UiModel lower;lower.setScreen({child,DisplayMode::View,1,data,0,ActionResult::None});
        for(const char key:{'1','2','3'}) lower.keyPressed(mapPhysicalKey(key,false),10);
        check(lower.snapshot().action.kind==ActionKind::None,"log children ignore screen shortcuts");
        check(!rootScreenTransition(child,SettingsPage::Root,ScreenId::Settings),"App rejects root jumps from log children");
        lower.keyPressed(UiKey::Escape,11);
        check(lower.snapshot().action.target==(child==ScreenId::LogDetail?ScreenId::Logs:ScreenId::LogDetail),"child Esc goes back exactly one level");
    }
    UiModel child;child.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    child.keyPressed(UiKey::Enter,10);
    for(const char key:{'1','2','3'}) child.keyPressed(mapPhysicalKey(key,false),11);
    check(child.snapshot().page==SettingsPage::Operation && child.snapshot().action.kind==ActionKind::None,"Settings child ignores shortcuts");
    check(!rootScreenTransition(ScreenId::Settings,SettingsPage::Operation,ScreenId::LivePlace),"App rejects jumps from Settings children");
    child.keyPressed(UiKey::Enter,12);child.keyPressed(UiKey::Right,13);
    child.keyPressed(UiKey::OpenLivePlace,14);child.keyPressed(UiKey::Escape,15);
    check(!child.editing() && child.snapshot().page==SettingsPage::Operation && child.snapshot().action.kind==ActionKind::None,"Esc cancels an edit instead of jumping");
    child.keyPressed(UiKey::Escape,16);
    check(child.snapshot().page==SettingsPage::Root && child.snapshot().action.kind==ActionKind::None,"Esc backs out to Settings root");
    data.browse.deleting=true;
    UiModel deleting;deleting.setScreen({ScreenId::Logs,DisplayMode::View,1,data,0,ActionResult::None});
    for(const char key:shortcuts) deleting.keyPressed(mapPhysicalKey(key,false),10);
    check(deleting.snapshot().action.kind==ActionKind::None,"accepted deletion cannot be left by any shortcut");
    data.browse.deleting=false;data.browse.loading=true;
    UiModel reading;reading.setScreen({ScreenId::Logs,DisplayMode::View,1,data,0,ActionResult::None});
    reading.keyPressed(UiKey::OpenGpsLogger,10);
    check(reading.snapshot().action.target==ScreenId::GpsLogger,"read-only loading does not block direct navigation");
    data.browse.loading=false;
    data.boot.maintenance=true;
    UiModel maintenance;maintenance.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    for(const char key:{'1','2','3'}) maintenance.keyPressed(mapPhysicalKey(key,false),10);
    check(maintenance.snapshot().action.kind==ActionKind::None,"maintenance cannot jump into GPS logger screens");
    maintenance.keyPressed(UiKey::Escape,11);
    check(maintenance.dialog().kind()==DialogKind::RestartLogger,"maintenance Esc still asks to restart");
    data.boot.maintenance=false;
    UiModel pause;
    pause.setScreen({ScreenId::LivePlace, DisplayMode::View, 1, data, 0, ActionResult::None});
    pause.keyPressed(mapPhysicalKey('1', true), 9);
    check(!pause.confirming() && pause.snapshot().action.kind == ActionKind::None, "F1 no longer pauses recording");
    pause.keyPressed(mapPhysicalKey('0', false), 9);
    check(!pause.confirming(), "0 no longer opens pause confirmation");
    pause.keyPressed(mapPhysicalKey('p', false), 10);
    check(pause.confirming() && pause.snapshot().action.kind == ActionKind::None, "P opens pause confirmation");
    for (const char key : {'1', '2', '3'}) pause.keyPressed(mapPhysicalKey(key, false), 20);
    check(pause.confirming() && pause.snapshot().action.kind == ActionKind::None, "shortcuts cannot bypass pause confirmation");
    pause.keyPressed(UiKey::Escape,20);
    check(!pause.confirming() && pause.snapshot().action.kind==ActionKind::None,"modal Esc cancels without opening Settings");
    pause.keyPressed(UiKey::PauseLog,20);
    for (const bool fn : {false, true}) {
        pause.keyPressed(mapPhysicalKey('/', fn), 21);
        check(pause.confirmChoice(), "slash selects the right dialog choice with or without Fn");
        pause.keyPressed(mapPhysicalKey(',', fn), 22);
        check(!pause.confirmChoice(), "comma selects Cancel with or without Fn");
        for (const char key : {';', '.'}) pause.keyPressed(mapPhysicalKey(key, fn), 23);
        check(!pause.confirmChoice(), "vertical physical keys do not change horizontal dialog choices");
    }
}

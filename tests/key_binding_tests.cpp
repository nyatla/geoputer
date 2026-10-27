#include <stdexcept>
#include "task/ui/KeyBindings.h"
#include "task/ui/UiModel.h"
#include "task/ui/ScreenNavigation.h"
using namespace geoputer;
namespace { void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);} }
void testKeyBindings() {
    UiData data{};data.settings=defaultSettings();data.storage.card_state=CardState::Mounted;
    data.gps.reception=GpsReception::Fix;data.gps.fix.valid=true;
    data.recording.state=RecordingState::Recording;
    const char keys[]={'1','2','3','4','0'};
    for(bool fn:{false,true}) {
        for(unsigned i=0;i<MenuCount;++i) {
            ScreenId target;check(screenShortcut(mapPhysicalKey(keys[i],fn),target) && target==MenuScreens[i].screen,"numeric root destination");
        }
        check(mapPhysicalKey('s',fn)==UiKey::ScreenOff && mapPhysicalKey('g',fn)==UiKey::GpsControl && mapPhysicalKey('p',fn)==UiKey::Playback,"S/G system controls and P playback");
        check(mapPhysicalKey('`',fn)==UiKey::Escape,"Fn-free physical Esc");
        check(mapPhysicalKey('+',fn)==UiKey::ZoomIn && mapPhysicalKey('=',fn)==UiKey::ZoomIn && mapPhysicalKey('-',fn)==UiKey::ZoomOut,"physical plus/equal and minus zoom bindings");
        check(mapPhysicalKey(' ',fn)==UiKey::Other && mapPhysicalKey('z',fn)==UiKey::Other,"former Space/Z bindings removed");
        check(mapPhysicalKey(';',fn)==UiKey::Up && mapPhysicalKey('.',fn)==UiKey::Down && mapPhysicalKey(',',fn)==UiKey::Left && mapPhysicalKey('/',fn)==UiKey::Right,"direction aliases");
        check(mapLicenseKey('.',fn,true)==UiKey::Right && mapLicenseKey(',',fn,true)==UiKey::Left && mapLicenseKey('.',fn,false)==UiKey::Down,"license angle bracket overrides");
    }
    for(auto from:{ScreenId::LivePlace,ScreenId::Map,ScreenId::GpsLogger,ScreenId::Logs,ScreenId::Settings,ScreenId::Menu}) {
        for(unsigned i=0;i<MenuCount;++i) {
            UiModel ui;ui.setScreen({from,DisplayMode::View,1,data,0,ActionResult::None});
            ui.keyPressed(mapPhysicalKey(keys[i],false),1);auto a=ui.snapshot().action;
            check(from==MenuScreens[i].screen?a.kind==ActionKind::None:a.kind==ActionKind::ChangeScreen && a.target==MenuScreens[i].screen,"direct root transition");
            check(ui.data().recording.state==RecordingState::Recording,"navigation changed recording");
        }
        UiModel ui;ui.setScreen({from,DisplayMode::View,1,data,0,ActionResult::None});
        ui.keyPressed(UiKey::Escape,1);auto a=ui.snapshot().action;
        check(a.kind==ActionKind::ChangeScreen && a.target==(from==ScreenId::Menu?ScreenId::LivePlace:ScreenId::Menu),"Esc menu/back");
    }
    UiModel back;back.setScreen({ScreenId::GpsLogger,DisplayMode::View,1,data,0,ActionResult::None});
    back.keyPressed(UiKey::Back,1);check(back.snapshot().action.kind==ActionKind::ChangeScreen && back.snapshot().action.target==ScreenId::LivePlace,"GPS Del returns to main");
    for(unsigned i=0;i<MenuCount;++i) {
        UiModel ui;ui.setScreen({ScreenId::Menu,DisplayMode::View,1,data,0,ActionResult::None});
        for(unsigned row=0;row<i;++row)ui.keyPressed(UiKey::Down,1);
        ui.keyPressed(UiKey::Enter,2);check(ui.snapshot().action.target==MenuScreens[i].screen,"menu selection equals shortcut");
    }
    for(auto child:{ScreenId::LogPoint,ScreenId::Settings}) {
        UiModel ui;ui.setScreen({child,DisplayMode::View,1,data,0,ActionResult::None});
        if(child==ScreenId::Settings)ui.keyPressed(UiKey::Enter,1);
        for(char key:keys)ui.keyPressed(mapPhysicalKey(key,false),2);
        check(ui.snapshot().action.kind==ActionKind::None,"child screens ignore numeric shortcuts");
        ui.keyPressed(UiKey::GpsControl,3);check(ui.confirming(),"G works on child screens too");
        ui.keyPressed(UiKey::OpenLivePlace,4);check(ui.snapshot().action.kind==ActionKind::None,"modal owns numeric shortcuts");
        ui.keyPressed(UiKey::Escape,5);check(!ui.confirming(),"modal Esc cancels");
        ui.keyPressed(UiKey::ScreenOff,6);check(ui.snapshot().action.kind==ActionKind::Sleep,"S on child screen");
    }
    UiModel sleeping;sleeping.setScreen({ScreenId::Map,DisplayMode::Background,1,data,0,ActionResult::None});
    sleeping.keyPressed(UiKey::GpsControl,1);auto wake=sleeping.snapshot().action;
    check(wake.kind==ActionKind::Wake && !sleeping.confirming(),"first wake key performs no operation");
    sleeping.updateContent({DisplayMode::View,1,data,wake.action_id,ActionResult::Applied});
    check(sleeping.snapshot().screen==ScreenId::Map && sleeping.snapshot().action.kind==ActionKind::None,"wake preserves screen");
    data.boot.maintenance=true;UiModel maintenance;maintenance.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    for(auto key:{UiKey::GpsControl,UiKey::ScreenOff,UiKey::OpenMap})maintenance.keyPressed(key,1);
    check(!maintenance.confirming() && maintenance.snapshot().action.kind==ActionKind::None,"maintenance blocks system shortcuts");
    maintenance.keyPressed(UiKey::Escape,2);check(maintenance.dialog().kind()==DialogKind::RestartLogger,"maintenance exit preserved");
}

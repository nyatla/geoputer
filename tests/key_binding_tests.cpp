#include <stdexcept>
#include <string>
#include "task/ui/KeyBindings.h"
#include "task/ui/UiModel.h"
#include "task/ui/ScreenNavigation.h"
#include "LogReplay.h"
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
        check(mapPhysicalKey('v',fn)==UiKey::Other && mapPhysicalKey('\t',fn)==UiKey::Tab,"V removed and ASCII Tab normalized");
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
        check(a.kind==ActionKind::ChangeScreen && a.target==(from==ScreenId::LivePlace?ScreenId::Menu:ScreenId::LivePlace),"only Live Esc opens selector; other roots return Live");
    }
    for(auto from:{ScreenId::LivePlace,ScreenId::Map,ScreenId::GpsLogger,ScreenId::Logs,ScreenId::Settings,ScreenId::Menu}) {
        UiModel ui;ui.setScreen({from,DisplayMode::View,1,data,0,ActionResult::None});
        ui.keyPressed(UiKey::Back,1);const auto action=ui.snapshot().action;
        check(from==ScreenId::LivePlace?action.kind==ActionKind::None:
            action.kind==ActionKind::ChangeScreen && action.target==ScreenId::LivePlace,"Del is Back everywhere and inert on Live");
    }
    for(char physical:{'r'}) {
        const auto mapped=mapPhysicalKey(physical,false);bool delivered=false;
        for(auto key:LocalShortcutKeys)delivered|=mapped==key;
        check(delivered,"R is included in the actual UI poller's dispatch table");
    }
    check(mapPhysicalKey('m',false)==UiKey::Other,"former M menu key removed");
    for(unsigned i=0;i<MenuCount;++i) {
        UiModel cycle;cycle.setScreen({MenuScreens[i].screen,DisplayMode::View,1,data,0,ActionResult::None});
        cycle.keyPressed(UiKey::Tab,1);
        check(cycle.snapshot().action.kind==ActionKind::ChangeScreen && cycle.snapshot().action.target==MenuScreens[(i+1)%MenuCount].screen,"Tab cycles all top-level numeric destinations and wraps");
    }
    UiModel selector;selector.setScreen({ScreenId::Menu,DisplayMode::View,1,data,0,ActionResult::None});
    selector.keyPressed(UiKey::Tab,1);check(selector.snapshot().action.target==ScreenId::LivePlace,"MENU Tab begins at first numeric destination");
    UiModel back;back.setScreen({ScreenId::GpsLogger,DisplayMode::View,1,data,0,ActionResult::None});
    back.keyPressed(UiKey::Back,1);check(back.snapshot().action.kind==ActionKind::ChangeScreen && back.snapshot().action.target==ScreenId::LivePlace,"GPS Del returns to main");
    for(unsigned i=0;i<MenuCount;++i) {
        UiModel ui;ui.setScreen({ScreenId::Menu,DisplayMode::View,1,data,0,ActionResult::None});
        for(unsigned row=0;row<i;++row)ui.keyPressed(UiKey::Down,1);
        ui.keyPressed(UiKey::Enter,2);check(ui.snapshot().action.target==MenuScreens[i].screen,"menu selection equals shortcut");
    }
    for(auto child:{ScreenId::Settings}) {
        UiModel ui;ui.setScreen({child,DisplayMode::View,1,data,0,ActionResult::None});
        if(child==ScreenId::Settings)ui.keyPressed(UiKey::Enter,1);
        for(char key:keys)ui.keyPressed(mapPhysicalKey(key,false),2);
        ui.keyPressed(UiKey::Tab,2);
        check(ui.snapshot().action.kind==ActionKind::None,"settings child screens ignore numeric shortcuts");
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
    auto text=[](const UiShortcutGuide& row) {
        std::string result;
        for(unsigned i=0;i<row.size();++i) {
            const auto key=row[i].key;
            const char c=key==UiKey::Escape?'@':key==UiKey::Enter?'!':key==UiKey::Tab?'~':key==UiKey::Left?'<':key==UiKey::Right?'>':
                key==UiKey::Up?'^':key==UiKey::Down?'v':shortcutCharacter(key);
            check(c!=0 && key!=UiKey::Back && key!=UiKey::Tab,"footer contains only explicit shortcut keys");result+=c;
        }
        check(result.empty() || (result.front()!=' ' && result.back()!=' ' && result.find("  ")==std::string::npos),"only one separator between nonempty groups");
        check(4+row.pixelWidth()<=236,"footer fits the display");return result;
    };
    for(const auto sample:{std::pair{ScreenId::LivePlace,"@SG 12340"},
            {ScreenId::Map,"@SG 12340 +-"},{ScreenId::GpsLogger,"@SG 12340 !"},
            {ScreenId::Logs,"@SG 12340 !D^v"},{ScreenId::Menu,"@SG 12340 !^v"},
            {ScreenId::Settings,"@SG 12340 !^v"},{ScreenId::LogProperties,"@SG 123 !"},
            {ScreenId::LogStatistics,"@SG 123 R"},{ScreenId::Boot,"@"},{ScreenId::UsbStorage,"@ !^v"}}) {
        UiModel ui;ui.setScreen({sample.first,DisplayMode::View,1,data,0,ActionResult::None});
        check(text(ui.shortcutGuide())==sample.second,"root/global/numeric/local footer order");
    }
    auto enterTone=[](const UiModel& model) {
        const auto row=model.shortcutGuide();
        for(unsigned i=0;i<row.size();++i)if(row[i].key==UiKey::Enter)return int(row[i].tone);
        return -1;
    };
    UiModel enter;enter.setScreen({ScreenId::LivePlace,DisplayMode::View,1,data,0,ActionResult::None});
    check(enterTone(enter)==-1,"LIVE has no Enter action");
    enter.setScreen({ScreenId::LogProperties,DisplayMode::View,2,data,0,ActionResult::None});
    check(enterTone(enter)==int(ShortcutTone::Disabled),"empty properties disable Enter");
    auto enter_data=data;enter_data.editBrowse().summary.records=1;
    enter.setScreen({ScreenId::LogProperties,DisplayMode::View,3,enter_data,0,ActionResult::None});
    check(enterTone(enter)==int(ShortcutTone::Normal),"nonempty properties enable Enter");
    enter_data.editBrowse().loading=true;
    enter.setScreen({ScreenId::Logs,DisplayMode::View,4,enter_data,0,ActionResult::None});
    check(enterTone(enter)==int(ShortcutTone::Disabled),"loading list disables Enter");
    enter_data.boot.complete=true;
    enter.setScreen({ScreenId::Boot,DisplayMode::View,5,enter_data,0,ActionResult::None});
    check(enterTone(enter)==int(ShortcutTone::Normal),"completed boot advertises Enter");
    enter.setScreen({ScreenId::Settings,DisplayMode::View,6,data,0,ActionResult::None});
    for(unsigned i=0;i<settingsRootIndex(SettingsRootItem::Licenses);++i)enter.keyPressed(UiKey::Down,10+i);
    enter.keyPressed(UiKey::Enter,20);
    check(enter.viewingLicenses() && enterTone(enter)==-1,"license scrolling has no Enter action");
    UiModel settings;settings.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    settings.keyPressed(UiKey::Enter,1);check(text(settings.shortcutGuide())=="@SG <>^v","scalar advertises direct edit and row navigation without Enter");
    settings.keyPressed(UiKey::Right,2);check(text(settings.shortcutGuide())=="@SG !<>^v","dirty scalar keeps row navigation and optional Enter confirmation");
    data.editBrowse().summary.records=100;data.editBrowse().target=10;data.dataset.state=PlaceState::Ready;data.settings.dataset=1;
    data.storage.log.open=false;
    UiModel point;point.setScreen({ScreenId::LogPoint,DisplayMode::View,1,data,0,ActionResult::None});
    check(text(point.shortcutGuide())=="@SG 123 BE<>^v","text viewer local ordering");
    point.keyPressed(UiKey::GpsControl,1);check(point.dialog().kind()==DialogKind::GpsControl,"G opens GPS choices");
    check(text(point.shortcutGuide())=="@ !<>","modal excludes system and numeric keys");
    point.keyPressed(UiKey::Back,2);check(!point.confirming(),"Del cancels view menu");
    data.editBrowse().map_mode=true;
    point.setScreen({ScreenId::LogPoint,DisplayMode::View,2,data,0,ActionResult::None});
    check(text(point.shortcutGuide())=="@SG 123 BE+-P<>^v","map viewer local ordering");
    for(auto screen:{ScreenId::LogProperties,ScreenId::LogPoint,ScreenId::LogStatistics}) {
        for(const auto& item:LogViewShortcuts) {
            UiModel local;local.setScreen({screen,DisplayMode::View,1,data,0,ActionResult::None});
            local.keyPressed(item.key,1);const auto action=local.snapshot().action;
            const bool current=selectedLogView(screen,data,item.action);
            check(current?action.kind==ActionKind::None:action.kind==ActionKind::Browse && action.browse_action==item.action,
                  "child numeric keys select local views, never top-level screens");
        }
        UiModel local;local.setScreen({screen,DisplayMode::View,1,data,0,ActionResult::None});
        local.keyPressed(UiKey::OpenLogs,1);local.keyPressed(UiKey::OpenSettings,2);
        check(local.snapshot().action.kind==ActionKind::None,"4/0 stay inactive in log child views");
        local.keyPressed(UiKey::GpsControl,3);local.keyPressed(UiKey::OpenMap,4);local.keyPressed(UiKey::Tab,5);
        check(local.confirming() && local.snapshot().action.kind==ActionKind::None,"modal owns child numeric keys");
    }
    auto unavailable=data;unavailable.settings.dataset=0;
    UiModel blocked;blocked.setScreen({ScreenId::LogProperties,DisplayMode::View,1,unavailable,0,ActionResult::None});
    blocked.keyPressed(UiKey::OpenMap,1);
    check(blocked.snapshot().action.kind==ActionKind::None,"local map shortcut blocked without dataset");
    for(unsigned i=0;i<blocked.shortcutGuide().size();++i)if(blocked.shortcutGuide()[i].key==UiKey::OpenMap)
        check(blocked.shortcutGuide()[i].tone==ShortcutTone::Disabled,"unavailable map number is grey");
    unavailable=data;unavailable.editBrowse().loading=true;
    for(const auto& item:LogViewShortcuts) {
        UiModel loading;loading.setScreen({ScreenId::LogProperties,DisplayMode::View,1,unavailable,0,ActionResult::None});
        loading.keyPressed(item.key,1);check(loading.snapshot().action.kind==ActionKind::None,"local view changes wait for current read");
    }
    auto tab_data=data;tab_data.editBrowse().loading=false;tab_data.editBrowse().error=LogReadError::None;
    tab_data.editBrowse().map_mode=false;
    for(const auto sample:{std::pair{ScreenId::LogProperties,BrowseAction::Text},
                          {ScreenId::LogPoint,BrowseAction::Map},{ScreenId::LogStatistics,BrowseAction::Text}}) {
        UiModel cycle;cycle.setScreen({sample.first,DisplayMode::View,1,tab_data,0,ActionResult::None});
        cycle.keyPressed(UiKey::Tab,1);
        check(cycle.snapshot().action.kind==ActionKind::Browse && cycle.snapshot().action.browse_action==sample.second,"Tab chooses next local numeric view and wraps stats to text");
    }
    tab_data.editBrowse().map_mode=true;
    UiModel map_cycle;map_cycle.setScreen({ScreenId::LogPoint,DisplayMode::View,1,tab_data,0,ActionResult::None});
    map_cycle.keyPressed(UiKey::Tab,1);check(map_cycle.snapshot().action.browse_action==BrowseAction::Statistics,"map Tab selects statistics");
    tab_data.editBrowse().map_mode=false;tab_data.settings.dataset=0;
    UiModel skip;skip.setScreen({ScreenId::LogPoint,DisplayMode::View,1,tab_data,0,ActionResult::None});
    skip.keyPressed(UiKey::Tab,1);check(skip.snapshot().action.browse_action==BrowseAction::Statistics,"Tab skips unavailable local map");
    tab_data.editBrowse().summary.records=0;
    UiModel only;only.setScreen({ScreenId::LogStatistics,DisplayMode::View,1,tab_data,0,ActionResult::None});
    only.keyPressed(UiKey::Tab,1);check(only.snapshot().action.kind==ActionKind::None,"Tab does not reopen its only available destination");
    const auto only_guide=only.shortcutGuide();
    for(unsigned i=0;i<only_guide.size();++i)check(only_guide[i].key!=UiKey::Tab,"Tab remains an implicit key without a footer mark");
    tab_data=data;tab_data.editBrowse().loading=true;
    UiModel loading_tab;loading_tab.setScreen({ScreenId::LogPoint,DisplayMode::View,1,tab_data,0,ActionResult::None});
    loading_tab.keyPressed(UiKey::Tab,1);check(loading_tab.snapshot().action.kind==ActionKind::None,"Tab waits for the current log read");
    UiModel boot_tab;boot_tab.setScreen({ScreenId::Boot,DisplayMode::View,1,data,0,ActionResult::None});
    boot_tab.keyPressed(UiKey::Tab,1);check(boot_tab.snapshot().action.kind==ActionKind::None,"Boot has no numeric Tab navigation");
    UiModel wake_tab;wake_tab.setScreen({ScreenId::Map,DisplayMode::Background,1,data,0,ActionResult::None});
    wake_tab.keyPressed(UiKey::Tab,1);check(wake_tab.snapshot().action.kind==ActionKind::Wake,"first Tab only wakes display");
    // While playing, directional keys change playback, including at point edges.
    data.editBrowse().playing=true;data.editBrowse().target=data.editBrowse().index=0;
    for(const auto sample:{std::pair{UiKey::Up,BrowseAction::ReplayFaster},
            {UiKey::Down,BrowseAction::ReplaySlower},{UiKey::Left,BrowseAction::ReplayReverse}}) {
        UiModel ui;ui.setScreen({ScreenId::LogPoint,DisplayMode::View,1,data,0,ActionResult::None});
        ui.keyPressed(sample.first,1);check(ui.snapshot().action.kind==ActionKind::Browse &&
            ui.snapshot().action.browse_action==sample.second,"PLAY directions are controls even at first point");
    }
    data.editBrowse().replay_reverse=true;data.editBrowse().target=data.editBrowse().index=99;
    UiModel backwards;backwards.setScreen({ScreenId::LogPoint,DisplayMode::View,1,data,0,ActionResult::None});
    backwards.keyPressed(UiKey::Right,1);check(backwards.snapshot().action.browse_action==BrowseAction::ReplayForward,"Right selects forward at the last point");
    data.editBrowse().replay_speed=replay::SpeedCount-1;
    UiModel fastest;fastest.setScreen({ScreenId::LogPoint,DisplayMode::View,1,data,0,ActionResult::None});
    check(!fastest.keyGuide().up && fastest.keyGuide().down,"rate maximum grays Up");
    fastest.keyPressed(UiKey::Up,1);check(fastest.snapshot().action.kind==ActionKind::None,"rate maximum ignores repeated Up");
    data.editBrowse().replay_speed=0;
    UiModel slowest;slowest.setScreen({ScreenId::LogPoint,DisplayMode::View,1,data,0,ActionResult::None});
    check(slowest.keyGuide().up && !slowest.keyGuide().down,"rate minimum grays Down");
    slowest.keyPressed(UiKey::Down,1);check(slowest.snapshot().action.kind==ActionKind::None,"rate minimum ignores repeated Down");
    data.editBrowse().playing=false;data.editBrowse().target=data.editBrowse().index=50;
    for(const auto sample:{std::pair{UiKey::Left,49u},{UiKey::Right,51u},{UiKey::Up,40u},{UiKey::Down,60u}}) {
        UiModel ui;ui.setScreen({ScreenId::LogPoint,DisplayMode::View,1,data,0,ActionResult::None});
        ui.keyPressed(sample.first,1);check(ui.snapshot().action.browse_action==BrowseAction::Point &&
            ui.snapshot().action.browse_index==sample.second,"PAUSE directions keep one/ten point navigation");
    }
    data.editBrowse().replay_speed=config::LogReplayDefaultSpeed;data.editBrowse().replay_reverse=false;
    UiModel stats;stats.setScreen({ScreenId::LogStatistics,DisplayMode::View,1,data,0,ActionResult::None});
    stats.keyPressed(mapPhysicalKey('r',false),1);check(stats.snapshot().action.browse_action==BrowseAction::Recalculate,"R requests statistics refresh");
    UiModel modal;modal.setScreen({ScreenId::GpsLogger,DisplayMode::View,1,data,0,ActionResult::None});
    modal.keyPressed(UiKey::Enter,1);check(text(modal.shortcutGuide())=="@ !<>","GPS choices have global Escape and local directions only");
    check(text(sleeping.shortcutGuide())=="@SG 12340 +-","wake restores footer");
    data.boot.maintenance=true;UiModel maintenance;maintenance.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    for(auto key:{UiKey::GpsControl,UiKey::ScreenOff,UiKey::OpenMap,UiKey::Tab})maintenance.keyPressed(key,1);
    check(!maintenance.confirming() && maintenance.snapshot().action.kind==ActionKind::None,"maintenance blocks system shortcuts");
    maintenance.keyPressed(UiKey::Escape,2);check(maintenance.dialog().kind()==DialogKind::RestartLogger,"maintenance exit preserved");
}

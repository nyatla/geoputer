#include "task/ui/GpsUiModel.h"
#include "task/ui/SettingsText.h"
#include <cstring>
#include <stdexcept>
using namespace geoputer;
namespace {void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}}
void testUiNotices(){
 UiData data{};data.settings=defaultSettings();data.settings.language=UiLanguage::English;data.settings.screen_timeout=4;
 GpsUiModel model;model.setScreen({ScreenId::LivePlace,DisplayMode::View,1,data,0,ActionResult::None});
 check(model.screenNameVisible()&&!std::strcmp(model.screenNameText(),"1 LIVE"),"initial view has a named nonmodal notice");
 model.tick(999);check(model.screenNameVisible(),"name remains for one second");model.takeRedraw();model.tick(1000);
 check(!model.screenNameVisible()&&model.takeRedraw(),"expiry redraws without waiting for incoming GPS data");
 model.setScreen({ScreenId::LivePlace,DisplayMode::View,2,data,0,ActionResult::None});check(!model.screenNameVisible(),"same screen refresh does not restart name window");
 model.setScreen({ScreenId::Map,DisplayMode::View,3,data,0,ActionResult::None});check(model.screenNameVisible()&&!std::strcmp(model.screenNameText(),"2 MAP"),"actual transition replaces the name");
 model.keyPressed(UiKey::OpenLogs,1001);check(model.snapshot().action.kind==ActionKind::ChangeScreen&&model.snapshot().action.target==ScreenId::Logs,"visible notice never consumes shortcut keys");
 auto id=model.snapshot().action.action_id;model.setScreen({ScreenId::Settings,DisplayMode::View,4,data,id,ActionResult::Applied});
 check(!model.screenNameVisible() && !*model.screenNameText(),"settings has no transition overlay");
 model.keyPressed(UiKey::Down,1010);model.keyPressed(UiKey::Down,1011);model.keyPressed(UiKey::Enter,1012);
 check(model.snapshot().page==SettingsPage::Alarm&&!model.screenNameVisible(),"third root item opens alerts without a child-view overlay");
 check(!std::strcmp(settingsRootLabel(UiLanguage::English,2),"Alerts"),"translated root labels follow the same third item");
 model.tick(2013);check(!model.screenNameVisible(),"submenu notice expires");
 model.tick(0xfffffff0u);model.setScreen({ScreenId::Logs,DisplayMode::View,5,data,0,ActionResult::None});
 model.tick(uint32_t(0xfffffff0u+999));check(model.screenNameVisible(),"notice handles tick wrap");
 model.tick(uint32_t(0xfffffff0u+1000));check(!model.screenNameVisible(),"notice expires across tick wrap");
 model.tick(0xfffffff0u);check(!model.screenNameVisible(),"expired notice does not resurrect after timer wrap");
 uint32_t generation=6;
 for(auto screen:{ScreenId::Menu,ScreenId::LogProperties,ScreenId::LogStatistics,ScreenId::LogPoint,ScreenId::Boot}) {
  model.setScreen({screen,DisplayMode::View,generation++,data,0,ActionResult::None});
  check(!model.screenNameVisible()&&! *model.screenNameText(),"menu, boot and log child screens never overlay the header");
 }
 for(auto screen:{ScreenId::LivePlace,ScreenId::Map,ScreenId::GpsLogger,ScreenId::Logs}) {
  model.setScreen({screen,DisplayMode::View,generation++,data,0,ActionResult::None});
  check(model.screenNameVisible()&&*model.screenNameText(),"all four numeric top screens have a transition overlay");
  const char* expected=model.screenNameText();data.settings.language=UiLanguage::Japanese;
  model.setScreen({screen,DisplayMode::View,generation++,data,0,ActionResult::None});
  check(!std::strcmp(model.screenNameText(),expected),"screen-name overlay stays English under Japanese UI");
  data.settings.language=UiLanguage::English;
 }
 data.settings.dataset=1;data.storage.alarm.dataset=1;data.storage.alarm.depth=4;data.storage.alarm.state=alarm::State::Ready;data.storage.alarm.settings.enabled=true;
 model.setScreen({ScreenId::LivePlace,DisplayMode::View,generation++,data,0,ActionResult::None});
 check(model.alarmSet()&&!model.alarmRinging(),"valid ON profile shows armed bell even without GPS FIX");
 model.setAudioPreview(true);model.setAudioActive(true);check(!model.alarmRinging(),"tone audition is not an active region alert");
 model.setAudioPreview(false);check(model.alarmRinging(),"real alert changes the bell to ringing");
 model.setAudioActive(false);data.storage.alarm.settings.enabled=false;model.setScreen({ScreenId::LivePlace,DisplayMode::View,generation++,data,0,ActionResult::None});
 check(!model.alarmSet(),"OFF profile hides bell");
}

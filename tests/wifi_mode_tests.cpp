#include "task/ui/GpsUiModel.h"
#include "BootMode.h"
#include "apps/wifi/WifiUiModel.h"
#include <Preferences.h>
#include <stdexcept>
using namespace geoputer;
namespace {void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}}
void testWifiMode() {
    UiData data{};data.settings=defaultSettings();data.storage.settings_state=SettingsState::Loaded;
    data.recording.state=RecordingState::Stopped;
    GpsUiModel ui;check(ui.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"Wi-Fi settings setup");
    ui.keyPressed(UiKey::Down,1);ui.keyPressed(UiKey::Enter,2); // System
    ui.keyPressed(UiKey::Right,3);ui.keyPressed(UiKey::Right,4);
    check(ui.editValue()==2 && ui.editDirty(),"third system mode is Wi-Fi");
    ui.keyPressed(UiKey::Down,5);check(ui.systemUsbMode()==2 && !ui.editing(),"move commits the Wi-Fi candidate");
    ui.keyPressed(UiKey::Down,6);ui.keyPressed(UiKey::Enter,7);
    check(ui.dialog().kind()==DialogKind::EnterWifiMode && !ui.dialog().selected(),"Wi-Fi restart confirmation defaults to cancel");
    ui.keyPressed(UiKey::Right,8);ui.keyPressed(UiKey::Enter,9);
    check(ui.snapshot().action.storage_operation==StorageOperation::EnterWifiMode && ui.snapshot().action.confirmed,"Wi-Fi mode operation is explicit");
    WifiUiModel wifi;wifi.update(WifiUiData{},0);
    for(auto key:{UiKey::OpenMap,UiKey::OpenLivePlace,UiKey::GpsControl,UiKey::ScreenOff,UiKey::Tab})wifi.key(key);
    check(wifi.action().kind==WifiActionKind::None,"Wi-Fi cannot activate GPS or sleep shortcuts");
    wifi.tick(999999);check(wifi.action().kind==WifiActionKind::None,"Wi-Fi does not autosleep");
    wifi.key(UiKey::Back);check(wifi.dialog().kind()==DialogKind::ExitWifiMode,"Del opens Wi-Fi exit confirmation");
    wifi.key(UiKey::Escape);check(!wifi.confirming(),"Esc cancels Wi-Fi exit");
    wifi.key(UiKey::Escape);wifi.key(UiKey::Right);wifi.key(UiKey::Enter);
    check(wifi.action().kind==WifiActionKind::Exit,"Wi-Fi exit action");
    WifiUiData ready{};ready.storage.revision=1;
    std::strcpy(ready.storage.wifi.url,"http://192.168.4.1/");ready.storage.wifi.phase=WifiPhase::Ready;
    WifiUiModel setup;setup.update(ready,0);
    check(setup.wifiPage()==WifiPage::ConnectionQr,"connection QR is initial page");
    setup.key(UiKey::Right);check(setup.wifiPage()==WifiPage::WebQr,"right selects URL QR");
    setup.key(UiKey::Right);check(setup.wifiPage()==WifiPage::Text,"right selects credentials text");
    setup.key(UiKey::Right);check(setup.wifiPage()==WifiPage::ConnectionQr,"pages wrap");
    setup.key(UiKey::Left);check(setup.wifiPage()==WifiPage::Text,"left wraps to text");
    setup.key(UiKey::Recalculate);check(setup.dialog().kind()==DialogKind::RegenerateWifi && !setup.dialog().selected(),"password renewal requires cancel-first confirmation");
    setup.key(UiKey::Escape);check(!setup.confirming() && setup.action().kind==WifiActionKind::None,"cancel never renews password");
    setup.key(UiKey::Recalculate);setup.key(UiKey::Right);setup.key(UiKey::Enter);
    check(setup.action().kind==WifiActionKind::Regenerate,"confirmed R renews credentials");
    WifiUiModel connected;connected.update(ready,0);
    ready.storage.wifi.clients=1;ready.storage.revision=2;
    connected.update(ready,0);
    check(connected.wifiPage()==WifiPage::WebQr,"first station connection selects URL QR");
    connected.key(UiKey::Right);ready.storage.revision=3;
    connected.update(ready,0);check(connected.wifiPage()==WifiPage::Text,"status updates preserve manual page choice");
    ready.storage.revision=4;ready.storage.wifi.phase=WifiPhase::Stopping;
    connected.update(ready,0);
    connected.key(UiKey::Recalculate);connected.key(UiKey::Escape);
    check(connected.action().kind==WifiActionKind::None,"restart phase rejects duplicate controls");
    const auto previous=Preferences::records;
    for(auto expected:{BootMode::GpsLogger,BootMode::UsbStorage,BootMode::WifiManagement}){check(saveBootMode(expected),"mode save");BootMode actual;check(loadBootMode(actual)&&actual==expected,"boot mode round trip");}
    check(!saveBootMode(static_cast<BootMode>(3)),"invalid boot mode rejected");
    Preferences::records=previous;
    check(!power::policy(power::Level::Normal,BootMode::WifiManagement).audio_available,"Wi-Fi does not start codec");
}

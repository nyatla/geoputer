#include "task/ui/UiModel.h"
#include "BootMode.h"
#include <Preferences.h>
#include <stdexcept>
using namespace geoputer;
namespace {void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}}
void testWifiMode() {
    UiData data{};data.settings=defaultSettings();data.storage.settings_state=SettingsState::Loaded;
    data.recording.state=RecordingState::Stopped;
    UiModel ui;check(ui.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"Wi-Fi settings setup");
    ui.keyPressed(UiKey::Down,1);ui.keyPressed(UiKey::Enter,2); // System
    ui.keyPressed(UiKey::Right,3);ui.keyPressed(UiKey::Right,4);
    check(ui.editValue()==2 && ui.editDirty(),"third system mode is Wi-Fi");
    ui.keyPressed(UiKey::Down,5);check(ui.systemUsbMode()==2 && !ui.editing(),"move commits the Wi-Fi candidate");
    ui.keyPressed(UiKey::Down,6);ui.keyPressed(UiKey::Enter,7);
    check(ui.dialog().kind()==DialogKind::EnterWifiMode && !ui.dialog().selected(),"Wi-Fi restart confirmation defaults to cancel");
    ui.keyPressed(UiKey::Right,8);ui.keyPressed(UiKey::Enter,9);
    check(ui.snapshot().action.storage_operation==StorageOperation::EnterWifiMode && ui.snapshot().action.confirmed,"Wi-Fi mode operation is explicit");
    UiModel wifi;check(wifi.setScreen({ScreenId::WifiManagement,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"Wi-Fi screen accepted");
    for(auto key:{UiKey::OpenMap,UiKey::OpenLivePlace,UiKey::GpsControl,UiKey::ScreenOff,UiKey::Tab})wifi.keyPressed(key,10);
    check(wifi.snapshot().action.kind==ActionKind::None,"Wi-Fi cannot activate GPS or sleep shortcuts");
    wifi.tick(999999);check(wifi.snapshot().action.kind==ActionKind::None,"Wi-Fi does not autosleep");
    wifi.keyPressed(UiKey::Back,11);check(wifi.dialog().kind()==DialogKind::ExitWifiMode,"Del opens Wi-Fi exit confirmation");
    wifi.keyPressed(UiKey::Escape,12);check(!wifi.confirming(),"Esc cancels Wi-Fi exit");
    wifi.keyPressed(UiKey::Escape,13);wifi.keyPressed(UiKey::Right,14);wifi.keyPressed(UiKey::Enter,15);
    check(wifi.snapshot().action.storage_operation==StorageOperation::ExitWifiMode,"Wi-Fi exit action");
    UiData ready=data;ready.revision=1;
    std::strcpy(ready.storage.wifi.url,"http://192.168.4.1/");ready.storage.wifi.phase=WifiPhase::Ready;
    UiModel setup;setup.setScreen({ScreenId::WifiManagement,DisplayMode::View,1,ready,0,ActionResult::None});
    check(setup.wifiPage()==WifiPage::ConnectionQr,"connection QR is initial page");
    setup.keyPressed(UiKey::Right,1);check(setup.wifiPage()==WifiPage::WebQr,"right selects URL QR");
    setup.keyPressed(UiKey::Right,2);check(setup.wifiPage()==WifiPage::Text,"right selects credentials text");
    setup.keyPressed(UiKey::Right,3);check(setup.wifiPage()==WifiPage::ConnectionQr,"pages wrap");
    setup.keyPressed(UiKey::Left,4);check(setup.wifiPage()==WifiPage::Text,"left wraps to text");
    setup.keyPressed(UiKey::Recalculate,5);check(setup.dialog().kind()==DialogKind::RegenerateWifi && !setup.dialog().selected(),"password renewal requires cancel-first confirmation");
    setup.keyPressed(UiKey::Escape,6);check(!setup.confirming() && setup.snapshot().action.kind==ActionKind::None,"cancel never renews password");
    setup.keyPressed(UiKey::Recalculate,7);setup.keyPressed(UiKey::Right,8);setup.keyPressed(UiKey::Enter,9);
    check(setup.snapshot().action.storage_operation==StorageOperation::RegenerateWifi && setup.snapshot().action.confirmed,"confirmed R renews credentials");
    UiModel connected;connected.setScreen({ScreenId::WifiManagement,DisplayMode::View,1,ready,0,ActionResult::None});
    ready.storage.wifi.clients=1;ready.revision=2;
    connected.updateContent({DisplayMode::View,1,ready,0,ActionResult::None});
    check(connected.wifiPage()==WifiPage::WebQr,"first station connection selects URL QR");
    connected.keyPressed(UiKey::Right,10);ready.revision=3;
    connected.updateContent({DisplayMode::View,1,ready,0,ActionResult::None});check(connected.wifiPage()==WifiPage::Text,"status updates preserve manual page choice");
    ready.revision=4;ready.storage.wifi.phase=WifiPhase::Stopping;
    connected.updateContent({DisplayMode::View,1,ready,0,ActionResult::None});
    connected.keyPressed(UiKey::Recalculate,11);connected.keyPressed(UiKey::Escape,12);
    check(connected.snapshot().action.kind==ActionKind::None,"restart phase rejects duplicate controls");
    const auto previous=Preferences::records;
    for(auto expected:{BootMode::GpsLogger,BootMode::UsbStorage,BootMode::WifiManagement}){check(saveBootMode(expected),"mode save");BootMode actual;check(loadBootMode(actual)&&actual==expected,"boot mode round trip");}
    check(!saveBootMode(static_cast<BootMode>(3)),"invalid boot mode rejected");
    Preferences::records=previous;
    check(!power::policy(power::Level::Normal,BootMode::WifiManagement).audio_available,"Wi-Fi does not start codec");
}

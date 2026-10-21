#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <Preferences.h>
#include "BootMode.h"
#include "task/ui/UiModel.h"
#include "task/storage/usb/UsbBlockIo.h"
using namespace geoputer;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void persistence() {
    Preferences::records.clear(); Preferences::fail_open = Preferences::fail_read = Preferences::fail_write = false;
    BootMode mode = BootMode::UsbStorage;
    check(loadBootMode(mode) && mode == BootMode::GpsLogger, "absent boot flag defaults to normal mode");
    Preferences::records["geoputer/config"] = {42,17};
    check(saveBootMode(BootMode::UsbStorage) && loadBootMode(mode) && mode == BootMode::UsbStorage, "USB boot flag persists");
    check(Preferences::records["geoputer/config"] == std::vector<unsigned char>({42,17}), "mode writes leave the settings record intact");
    Preferences::fail_write = true;
    check(!saveBootMode(BootMode::GpsLogger) && loadBootMode(mode) && mode == BootMode::UsbStorage, "failed flag save retains USB mode");
    Preferences::fail_write = false;
    check(saveBootMode(BootMode::GpsLogger) && loadBootMode(mode) && mode == BootMode::GpsLogger, "exit flag survives reboot");
    Preferences::records["geoputer/boot_mode"][3] ^= 1;
    check(!loadBootMode(mode) && mode == BootMode::GpsLogger, "damaged flag falls back to normal mode");
    check(!saveBootMode(static_cast<BootMode>(9)), "unknown mode cannot be saved");
    Preferences::fail_open = true;
    check(!loadBootMode(mode) && mode == BootMode::GpsLogger && !saveBootMode(BootMode::UsbStorage), "NVS open failures never select USB accidentally");
    Preferences::fail_open = false;
    Preferences::fail_read = true;
    check(!loadBootMode(mode) && mode == BootMode::GpsLogger, "NVS read error defaults to normal mode");
    Preferences::fail_read = false;
}
void ui() {
    UiData data{}; data.settings = defaultSettings();
    auto openMode = [&](UiModel& model) {
        model.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
        for (unsigned n=0;n<SettingsModeRow;++n) model.keyPressed(UiKey::Down,n+2);
        model.keyPressed(UiKey::Enter,20);
    };
    UiModel model; openMode(model);
    check(model.dialog().kind() == DialogKind::EnterUsbMode && !model.confirmChoice(), "root mode entry opens a Cancel-default dialog");
    model.keyPressed(UiKey::Enter,21);
    check(model.snapshot().action.kind == ActionKind::None, "default Enter does not request a reboot");
    check(model.snapshot().page==SettingsPage::Root && model.snapshot().selected_setting==SettingsModeRow,
          "cancelling mode switch preserves the root mode selection");
    model.keyPressed(UiKey::Enter,22); model.keyPressed(UiKey::Right,23); model.keyPressed(UiKey::Enter,24);
    check(model.snapshot().action.kind == ActionKind::Storage && model.snapshot().action.confirmed &&
          model.snapshot().action.storage_operation == StorageOperation::EnterUsbMode, "confirmed entry asks Storage to change the flag");
    const auto accepted=model.snapshot().action;
    data.storage.busy=true;data.storage.operation=StorageOperation::EnterUsbMode;
    model.updateContent({DisplayMode::View,1,data,accepted.action_id,ActionResult::Applied});
    model.keyPressed(UiKey::Escape,25);model.keyPressed(UiKey::Enter,26);model.tick(27);model.tick(100000);
    check(model.snapshot().page==SettingsPage::Root && model.snapshot().action.kind==ActionKind::None && !model.confirming(),
          "accepted mode change blocks new navigation/actions and sleep until its completion");
    data.storage.busy=false;
    model.updateContent({DisplayMode::View,1,data,0,ActionResult::None});model.keyPressed(UiKey::Enter,100001);
    check(model.confirming(),"save failure or rejection can restore interactive retry without restarting");
    data.storage.operation=StorageOperation::None;
    for (auto state : {RecordingState::Recording,RecordingState::Paused,RecordingState::Starting}) {
        data.recording.state = state; UiModel blocked; openMode(blocked);
        blocked.keyPressed(UiKey::Right,30); blocked.keyPressed(UiKey::Enter,31);
        check(blocked.snapshot().action.kind == ActionKind::None, "recording, paused and starting cannot switch to USB");
    }
    data.recording.state = RecordingState::Stopped;
    data.storage.log.closing = true; check(!usbEntryAllowed(data), "closing session blocks entry"); data.storage.log.closing = false;
    data.storage.log.buffered = 1; check(!usbEntryAllowed(data), "unsaved ring records block entry"); data.storage.log.buffered = 0;
    data.storage.log.open = true; check(!usbEntryAllowed(data), "retained session blocks entry"); data.storage.log.open = false;
    data.recording.pending = true; check(!usbEntryAllowed(data), "pending App sample blocks entry"); data.recording.pending = false;
    data.dataset.state = PlaceState::TooManyChunks; // USB mode does not require a usable dataset or SD card.
    UiModel usb; usb.setScreen({ScreenId::UsbStorage,DisplayMode::View,1,data,0,ActionResult::None});
    check(!usb.confirming() && usb.usbPage()==UsbPage::Menu, "USB boot shows menu independent of GIS");
    usb.tick(2); usb.tick(100000);
    check(usb.snapshot().action.kind == ActionKind::None && !usb.confirming(), "USB menu never sleeps automatically");
    for (auto key : {UiKey::OpenGpsLogger,UiKey::OpenSettings,UiKey::OpenLogs,UiKey::F1,UiKey::PauseLog,UiKey::DeleteLog}) usb.keyPressed(key,100001);
    check(usb.snapshot().action.kind == ActionKind::None, "normal shortcuts cannot leave the USB screen");
    usb.keyPressed(UiKey::Escape,100002); usb.keyPressed(UiKey::Right,100003); usb.keyPressed(UiKey::Enter,100004);
    check(usb.snapshot().action.kind == ActionKind::Storage && usb.snapshot().action.confirmed &&
          usb.snapshot().action.storage_operation == StorageOperation::ExitUsbMode, "exit requests flag save and reboot through Storage");
}
void exportUi() {
    UiData data{};data.settings=defaultSettings();data.storage.usb.ready=true;data.storage.usb.connected=true;
    UiModel ui;ui.setScreen({ScreenId::UsbStorage,DisplayMode::View,1,data,0,ActionResult::None});
    UiModel exit;exit.setScreen({ScreenId::UsbStorage,DisplayMode::View,1,data,0,ActionResult::None});
    exit.keyPressed(UiKey::Escape,1);exit.keyPressed(UiKey::Right,2);
    check(exit.dialog().actionEnabled() && exit.confirmChoice(),"connected PC does not disable explicitly confirmed exit");
    exit.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    check(exit.confirmChoice(),"USB state updates do not clear the selected Exit choice");
    exit.keyPressed(UiKey::Enter,3);
    check(exit.snapshot().action.kind==ActionKind::Storage && exit.snapshot().action.confirmed &&
          exit.snapshot().action.storage_operation==StorageOperation::ExitUsbMode,
          "exit can be requested without an OS eject notification");
    ui.keyPressed(UiKey::Enter,5);
    check(ui.snapshot().action.kind==ActionKind::UsbExport && ui.snapshot().action.usb_action==UsbAction::Begin,
          "menu requests asynchronous preparation");
    auto ack=[&](ExportPhase phase) {
        const auto id=ui.snapshot().action.action_id;data.storage.export_job.phase=phase;
        ui.updateContent({DisplayMode::View,1,data,id,ActionResult::Applied});
    };
    ack(ExportPhase::WaitEject);
    check(ui.usbPage()==UsbPage::WaitEject,"connected export waits for safe removal");
    ui.keyPressed(UiKey::Escape,6);
    check(ui.snapshot().action.kind==ActionKind::UsbExport && ui.snapshot().action.usb_action==UsbAction::Publish,"safe-removal wait can be cancelled");
    ack(ExportPhase::Idle);data.storage.usb.connected=false;
    ui.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    ui.keyPressed(UiKey::Enter,7);ack(ExportPhase::Preparing);
    check(ui.usbPage()==UsbPage::Busy,"disconnected entry skips safe-removal prompt");
    data.storage.usb.local=true;data.browse.revision=7;data.browse.count=4;data.browse.has_older=true;
    for(unsigned n=0;n<4;++n)std::strcpy(data.browse.names[n],"20261017_093012_0.gps.dat");
    ack(ExportPhase::Selecting);
    check(ui.usbPage()==UsbPage::Files,"prepared local storage exposes file selection");
    ui.keyPressed(UiKey::Down,8);ui.keyPressed(UiKey::Down,9);ui.keyPressed(UiKey::Enter,10);
    check(ui.usbPage()==UsbPage::Format && ui.usbFile()==2,"file selection is pinned before choosing format");
    ui.keyPressed(UiKey::Left,11);ui.keyPressed(UiKey::Down,12);ui.keyPressed(UiKey::Enter,13);
    check(ui.snapshot().action.kind==ActionKind::UsbExport && ui.snapshot().action.usb_action==UsbAction::Start && ui.snapshot().action.export_format==ExportFormat::Kml &&
          ui.snapshot().action.browse_index==2 && ui.snapshot().action.browse_revision==7,"format request preserves selected file and list revision");
    ack(ExportPhase::Running);ui.keyPressed(UiKey::Escape,14);ui.keyPressed(UiKey::Enter,15);
    check(ui.usbPage()==UsbPage::Progress && ui.snapshot().action.kind==ActionKind::None,"conversion blocks premature publish and exit");
    data.storage.usb.local=false;ack(ExportPhase::Complete);
    data.storage.usb.connected=true;ui.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    check(ui.usbPage()==UsbPage::Complete,"reconnection updates completion without reopening a job");
    ui.keyPressed(UiKey::Enter,16);ack(ExportPhase::Publishing);ack(ExportPhase::Idle);
    check(ui.usbPage()==UsbPage::Menu,"completed export returns to USB menu");
    data.storage.usb.ejected=true;ui.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    check(!usbHostNeedsEject(data.storage.usb),"safe removal permits local ownership even if cable remains attached");
    ui.keyPressed(UiKey::Escape,17);ui.keyPressed(UiKey::Right,18);ui.keyPressed(UiKey::Enter,19);
    check(ui.snapshot().action.storage_operation==StorageOperation::ExitUsbMode,"safely ejected exit reaches flag-save request");
}
void blocks() {
    std::array<uint8_t,1536> disk{}, expected{}; uint8_t scratch[512]; unsigned reads=0,writes=0;
    for (unsigned n=0;n<disk.size();++n) disk[n]=static_cast<uint8_t>(n*31u);
    auto read=[&](uint8_t* p,uint32_t sector){++reads;if(sector>=3)return false;std::memcpy(p,disk.data()+sector*512,512);return true;};
    auto write=[&](const uint8_t* p,uint32_t sector){++writes;if(sector>=3)return false;std::memcpy(disk.data()+sector*512,p,512);return true;};
    std::array<uint8_t,1200> data{};
    check(transferUsbBlocks(3,0,500,data.data(),800,false,read,write,scratch)==800 &&
          !std::memcmp(data.data(),disk.data()+500,800), "cross-sector unaligned reads preserve byte order");
    expected=disk; std::fill(data.begin(),data.end(),0x5a);std::fill(expected.begin()+500,expected.begin()+1300,0x5a);
    check(transferUsbBlocks(3,0,500,data.data(),800,true,read,write,scratch)==800 && disk==expected,
          "partial writes preserve neighboring sector bytes");
    reads=writes=0;
    check(transferUsbBlocks(3,2,500,data.data(),20,true,read,write,scratch)==-1 && !reads && !writes,
          "entire invalid write range is rejected before touching SD");
    check(transferUsbBlocks(3,UINT32_MAX,UINT32_MAX,data.data(),512,false,read,write,scratch)==-1 && !reads,
          "address arithmetic cannot wrap into the start of the card");
    check(transferUsbBlocks(3,3,0,nullptr,0,false,read,write,scratch)==0, "empty I/O at the end is bounded");
    check(transferUsbBlocks(3,0,0,data.data(),512,true,read,write,scratch)==512 && !reads && writes==1,
          "full-sector writes require no read-modify-write");
    auto failRead=[](uint8_t*,uint32_t){return false;}; auto failWrite=[](const uint8_t*,uint32_t){return false;};
    check(transferUsbBlocks(3,0,0,data.data(),512,false,failRead,write,scratch)==-1, "read failures propagate");
    check(transferUsbBlocks(3,0,0,data.data(),512,true,read,failWrite,scratch)==-1, "write failures propagate");
    uint32_t random=91;
    for (unsigned n=0;n<1000;++n) {
        random=random*1664525u+1013904223u; const unsigned start=random%1536;
        random=random*1664525u+1013904223u; const unsigned count=random%std::min<unsigned>(1200,1536-start)+1;
        const bool writing=n%2;
        expected=disk;
        if (writing) { for(unsigned i=0;i<count;++i)data[i]=static_cast<uint8_t>(n+i);std::copy_n(data.begin(),count,expected.begin()+start); }
        check(transferUsbBlocks(3,start/512,start%512,data.data(),count,writing,read,write,scratch)==static_cast<int32_t>(count), "random block transfer completes");
        check(writing?disk==expected:!std::memcmp(data.data(),expected.data()+start,count), "block transfers match the reference byte array");
    }
}
}
void testUsbMode() { persistence(); ui(); exportUi(); blocks(); }

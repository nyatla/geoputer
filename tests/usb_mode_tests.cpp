#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <Preferences.h>
#include "BootMode.h"
#include "apps/usb/UsbUiModel.h"
#include "task/ui/GpsUiModel.h"
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
    UiData data{}; data.settings = defaultSettings(); data.storage.settings_state = SettingsState::Loaded;
    auto openMode = [&](GpsUiModel& model) {
        model.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
        for (unsigned n=0;n<SettingsSystemRow;++n) model.keyPressed(UiKey::Down,n+2);
        model.keyPressed(UiKey::Enter,20);
    };
    auto stageUsb = [&](GpsUiModel& model) {
        openMode(model);
        check(model.snapshot().page == SettingsPage::System && !model.confirming(), "System opens a submenu");
        model.keyPressed(UiKey::Right,22);
        check(model.editDirty() && model.editValue() == 1 && !model.systemUsbMode(), "mode changes directly and stays local until confirmed");
        model.keyPressed(UiKey::Down,23);
        check(model.systemUsbMode() && !model.editDirty() && model.snapshot().selected_setting==1 && model.snapshot().action.kind == ActionKind::None,
              "moving from mode confirms the local candidate without persisting or rebooting");
        model.keyPressed(UiKey::Enter,25);
        check(!model.editing() && model.snapshot().action.kind == ActionKind::None, "GPS source remains read-only");
        model.keyPressed(UiKey::Down,26);
    };
    GpsUiModel draft; openMode(draft);
    draft.keyPressed(UiKey::Enter,21); draft.keyPressed(UiKey::Right,22); draft.keyPressed(UiKey::Escape,23);
    check(!draft.systemUsbMode() && !draft.editing(), "Escape discards the mode preview");
    draft.keyPressed(UiKey::Down,24); draft.keyPressed(UiKey::Down,25); draft.keyPressed(UiKey::Enter,26);
    check(!draft.confirming(), "unchanged mode cannot save and restart");
    draft.keyPressed(UiKey::Escape,27);
    check(draft.snapshot().page==SettingsPage::Root && draft.snapshot().selected_setting==SettingsSystemRow,
          "System back preserves the root selection");
    GpsUiModel model; stageUsb(model); model.keyPressed(UiKey::Enter,28);
    check(model.dialog().kind() == DialogKind::EnterUsbMode && !model.confirmChoice(), "Save opens a Cancel-default dialog");
    model.keyPressed(UiKey::Enter,29);
    check(model.snapshot().action.kind == ActionKind::None && model.systemUsbMode(), "cancelling confirmation retains the draft without reboot");
    model.keyPressed(UiKey::Enter,30); model.keyPressed(UiKey::Right,31); model.keyPressed(UiKey::Enter,32);
    check(model.snapshot().action.kind == ActionKind::Storage && model.snapshot().action.confirmed &&
          model.snapshot().action.storage_operation == StorageOperation::EnterUsbMode, "confirmed Save asks Storage to persist boot mode");
    const auto accepted=model.snapshot().action;
    data.storage.busy=true;data.storage.operation=StorageOperation::EnterUsbMode;
    model.updateContent({DisplayMode::View,1,data,accepted.action_id,ActionResult::Applied});
    model.keyPressed(UiKey::Escape,33);model.keyPressed(UiKey::Enter,34);model.tick(35);model.tick(100000);
    check(model.snapshot().page==SettingsPage::System && model.snapshot().action.kind==ActionKind::None && !model.confirming(),
          "accepted mode change blocks navigation and sleep until completion");
    data.storage.busy=false;data.storage.operation=StorageOperation::None;
    model.updateContent({DisplayMode::View,1,data,0,ActionResult::None});model.keyPressed(UiKey::Enter,100001);
    check(model.confirming(),"save failure or rejection permits retry without restarting");
    data.recording.state=RecordingState::Recording;
    model.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    check(!model.dialog().actionEnabled(),"a changed prerequisite disables an open confirmation");
    model.keyPressed(UiKey::Right,100002);model.keyPressed(UiKey::Enter,100003);
    check(model.snapshot().action.kind==ActionKind::None,"disabled confirmation cannot request mode switch");
    for (auto state : {RecordingState::Recording,RecordingState::Paused,RecordingState::Starting}) {
        data.recording.state = state; GpsUiModel blocked; stageUsb(blocked);
        blocked.keyPressed(UiKey::Enter,30);
        check(!blocked.confirming() && blocked.snapshot().action.kind == ActionKind::None, "recording, paused and starting block Save");
    }
    data.recording.state = RecordingState::Stopped;
    data.storage.log.closing = true; check(!usbEntryAllowed(data), "closing session blocks entry"); data.storage.log.closing = false;
    data.storage.log.buffered = 1; check(!usbEntryAllowed(data), "unsaved ring records block entry"); data.storage.log.buffered = 0;
    data.storage.log.open = true; check(!usbEntryAllowed(data), "retained session blocks entry"); data.storage.log.open = false;
    data.recording.pending = true; check(!usbEntryAllowed(data), "pending App sample blocks entry"); data.recording.pending = false;
    data.storage.busy = true; check(!usbEntryAllowed(data), "storage busy blocks entry"); data.storage.busy = false;
    data.editDatasets().loading = true; check(!usbEntryAllowed(data), "dataset read blocks entry"); data.editDatasets().loading = false;
    data.storage.settings_state=SettingsState::Unavailable;
    GpsUiModel noNvs; stageUsb(noNvs);noNvs.keyPressed(UiKey::Enter,30);
    check(!noNvs.confirming(),"known NVS unavailability blocks Save");
    data.storage.settings_state=SettingsState::Loaded;
    GpsUiModel leave; stageUsb(leave);leave.keyPressed(UiKey::Escape,30);leave.keyPressed(UiKey::Enter,31);
    check(!leave.systemUsbMode(),"leaving System discards unsaved draft");
    UsbUiModel usb; usb.update(UsbUiData{},0);
    check(!usb.confirming() && usb.usbPage()==UsbPage::Menu, "USB boot shows menu independent of GIS");
    usb.tick(2); usb.tick(100000);
    check(usb.action().kind == UsbActionKind::None && !usb.confirming(), "USB menu never sleeps automatically");
    for (auto key : {UiKey::OpenGpsLogger,UiKey::OpenSettings,UiKey::OpenLogs,UiKey::Other,UiKey::DeleteLog}) usb.key(key);
    check(usb.action().kind == UsbActionKind::None, "normal shortcuts cannot leave the USB screen");
    usb.key(UiKey::Escape); usb.key(UiKey::Right); usb.key(UiKey::Enter);
    check(usb.action().kind == UsbActionKind::Exit, "exit requests flag save and reboot through Storage");
}
void exportUi() {
    UsbUiData data{};data.storage.usb.ready=true;data.storage.usb.connected=true;
    UsbUiModel ui;ui.update(data,0);
    UsbUiModel exit;exit.update(data,0);
    exit.key(UiKey::Escape);exit.key(UiKey::Right);
    check(exit.dialog().actionEnabled() && exit.dialog().selected(),"connected PC does not disable explicitly confirmed exit");
    exit.update(data,0,ActionResult::None);
    check(exit.dialog().selected(),"USB state updates do not clear the selected Exit choice");
    exit.key(UiKey::Enter);
    check(exit.action().kind==UsbActionKind::Exit,
          "exit can be requested without an OS eject notification");
    ui.key(UiKey::Enter);
    check(ui.action().kind==UsbActionKind::Begin,
          "menu requests asynchronous preparation");
    auto ack=[&](ExportPhase phase) {
        const auto id=ui.action().id;data.storage.export_job.phase=phase;
        ui.update(data,id,ActionResult::Applied);
    };
    ack(ExportPhase::WaitEject);
    check(ui.usbPage()==UsbPage::WaitEject,"connected export waits for safe removal");
    ui.key(UiKey::Escape);
    check(ui.action().kind==UsbActionKind::Publish,"safe-removal wait can be cancelled");
    ack(ExportPhase::Idle);data.storage.usb.connected=false;
    ui.update(data,0,ActionResult::None);
    ui.key(UiKey::Enter);ack(ExportPhase::Preparing);
    check(ui.usbPage()==UsbPage::Busy,"disconnected entry skips safe-removal prompt");
    data.storage.usb.local=true;data.storage.files.revision=7;data.storage.files.count=4;data.storage.files.has_older=true;
    for(unsigned n=0;n<4;++n)std::strcpy(data.storage.files.names[n],"20261017_093012_0.gps.dat");
    ack(ExportPhase::Selecting);
    check(ui.usbPage()==UsbPage::Files,"prepared local storage exposes file selection");
    ui.key(UiKey::Down);ui.key(UiKey::Down);ui.key(UiKey::Enter);
    check(ui.usbPage()==UsbPage::Format && ui.usbFile()==2,"file selection is pinned before choosing format");
    ui.key(UiKey::Left);ui.key(UiKey::Down);ui.key(UiKey::Enter);
    check(ui.action().kind==UsbActionKind::Start && ui.action().format==ExportFormat::Kml &&
          ui.action().index==2,"format request preserves selected file and list revision");
    ack(ExportPhase::Running);ui.key(UiKey::Escape);ui.key(UiKey::Enter);
    check(ui.usbPage()==UsbPage::Progress && ui.action().kind==UsbActionKind::None,"conversion blocks premature publish and exit");
    data.storage.usb.local=false;ack(ExportPhase::Complete);
    data.storage.usb.connected=true;ui.update(data,0,ActionResult::None);
    check(ui.usbPage()==UsbPage::Complete,"reconnection updates completion without reopening a job");
    ui.key(UiKey::Enter);ack(ExportPhase::Publishing);ack(ExportPhase::Idle);
    check(ui.usbPage()==UsbPage::Menu,"completed export returns to USB menu");
    data.storage.usb.ejected=true;ui.update(data,0,ActionResult::None);
    check(!usbHostNeedsEject(data.storage.usb),"safe removal permits local ownership even if cable remains attached");
    ui.key(UiKey::Escape);ui.key(UiKey::Right);ui.key(UiKey::Enter);
    check(ui.action().kind==UsbActionKind::Exit,"safely ejected exit reaches flag-save request");
}
void blocks() {
    std::array<uint8_t,1536> disk{}, expected{}; uint8_t scratch[512]; unsigned reads=0,writes=0;
    for (unsigned n=0;n<disk.size();++n) disk[n]=static_cast<uint8_t>(n*31u);
    auto read=[&](uint8_t* p,uint32_t sector,uint32_t count){++reads;if(!count || sector>=3 || count>3-sector)return false;std::memcpy(p,disk.data()+sector*512,count*512);return true;};
    auto write=[&](const uint8_t* p,uint32_t sector,uint32_t count){++writes;if(!count || sector>=3 || count>3-sector)return false;std::memcpy(disk.data()+sector*512,p,count*512);return true;};
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
    auto failRead=[](uint8_t*,uint32_t,uint32_t){return false;}; auto failWrite=[](const uint8_t*,uint32_t,uint32_t){return false;};
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
    std::array<uint8_t,4096> bulkDisk{},bulkData{};
    unsigned bulkReads=0,bulkWrites=0;
    auto bulkRead=[&](uint8_t* p,uint32_t sector,uint32_t count){
        ++bulkReads;check(sector==0 && count==8,"aligned read uses one eight-sector operation");
        std::memcpy(p,bulkDisk.data(),bulkDisk.size());return true;
    };
    auto bulkWrite=[&](const uint8_t* p,uint32_t sector,uint32_t count){
        ++bulkWrites;check(sector==0 && count==8,"aligned write uses one eight-sector operation");
        std::memcpy(bulkDisk.data(),p,bulkDisk.size());return true;
    };
    for(unsigned n=0;n<bulkData.size();++n)bulkData[n]=static_cast<uint8_t>(n*17u);
    check(transferUsbBlocks(8,0,0,bulkData.data(),bulkData.size(),true,bulkRead,bulkWrite,scratch)==4096 &&
          bulkDisk==bulkData && bulkReads==0 && bulkWrites==1,"4 KiB write uses the MSC buffer directly");
    bulkData.fill(0);
    check(transferUsbBlocks(8,0,0,bulkData.data(),bulkData.size(),false,bulkRead,bulkWrite,scratch)==4096 &&
          bulkDisk==bulkData && bulkReads==1 && bulkWrites==1,"4 KiB read uses the MSC buffer directly");
}
}
void testUsbMode() { persistence(); ui(); exportUi(); blocks(); }

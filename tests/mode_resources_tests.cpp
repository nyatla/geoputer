#include "Application.h"
#include <cstdio>
#include <stdexcept>
using namespace geoputer;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
unsigned gps_built=0,gps_destroyed=0,usb_built=0,usb_destroyed=0;
struct GpsProbe {explicit GpsProbe(int n):value(n){++gps_built;}~GpsProbe(){++gps_destroyed;}int value;char bytes[256]{};};
struct UsbProbe {explicit UsbProbe(int n):value(n){++usb_built;}~UsbProbe(){++usb_destroyed;}int value;char bytes[128]{};};
}
namespace geoputer {
void UiTask::run(){}void GpsTask::run(){}void StorageTask::run(){}
void StorageTask::sdBegin(){}void StorageTask::sdEnd(){}void StorageTask::cacheState(RgCacheState,bool){}
}
int main(){
    {
        BootModeResources<GpsProbe,UsbProbe> memory;
        check(!memory.selected() && !gps_built && !usb_built,"global construction must not choose a mode");
        check(memory.selectGps(42) && memory.selectGps(99),"same startup mode is idempotent");
        check(memory.gps().value==42 && gps_built==1 && !usb_built,"GPS is constructed exactly once");
        check(!memory.selectUsb(10) && memory.gps().value==42,"live mode cannot be replaced");
        check(static_cast<void*>(&memory.data_.gps)==static_cast<void*>(&memory.data_.usb),"mode members share their address");
    }
    check(gps_destroyed==1 && !usb_destroyed,"destroy only the constructed GPS member");
    {
        BootModeResources<GpsProbe,UsbProbe> memory;
        check(memory.selectUsb(7) && !memory.selectGps(9) && memory.usb().value==7,"USB selection is exclusive");
    }
    check(usb_built==1 && usb_destroyed==1 && gps_built==1,"USB never constructs GPS");
    static_assert(sizeof(BootModeResources<GpsProbe,UsbProbe>)<sizeof(GpsProbe)+sizeof(UsbProbe),"reservation is maximum, not sum");
    {
        Application app;
        check(!app.mode_resources_.selected() && !app.storage_.mode_resources_.selected(),"App and Storage start with no active mode");
        check(app.selectBootMode(true) && app.mode_resources_.isUsb() && app.storage_.mode_resources_.isUsb(),"App and Storage choose USB together");
        check(&app.mode_resources_==&app.storage_.mode_resources_,"App and Storage borrow the same outer union");
        check(!app.storage_.usbMode().exporter_ && !app.storage_.work().mapBusy(),"USB starts without exporter allocation or map worker");
        app.screenResources().enter(ScreenGroup::Usb);
        check(app.screenResources().hasLogs(),"USB retains its own file-list resources");
        check(!app.selectBootMode(false) && app.usb_mode_,"App cannot replace resources before reboot");
        check(!app.storage_.setUsbMode(false),"Storage cannot activate the other member");
    }
    {
        Application app;check(app.selectBootMode(false),"select GPS before tasks");
        auto& gps=app.storage_.gpsMode();
        check(&gps.selected_dataset_==&gps.place_lookup_.selected_,"lookup borrows the selected dataset in the same mode");
        check(gps.screen_resources_.live_map.pixels==gps.code_images_.map.image().data() &&
              gps.screen_resources_.log_map.pixels==gps.code_images_.map.image().data(),"map sessions borrow the GPS member's stable raster");
        check(&app.storage_.work()==&gps.screen_resources_.work,"Reader workspace remains owned by Storage");
    }
    {
        Application app;check(app.storage_.setWifiMode(),"select Wi-Fi without GPS or USB");app.wifi_mode_=true;
        check(app.mode_resources_.isWifi() && !app.mode_resources_.isUsb(),"Wi-Fi selects the third union branch");
        app.screenResources().enter(ScreenGroup::Common);
        check(!app.storage_.setWifiMode() && !app.storage_.setUsbMode(false),"Wi-Fi resources cannot change while active");
        check(app.storage_.inbox().initialize() && app.storage_.requestWifiExit(),"Wi-Fi exit is a queued control, no shared mutable service access");
        StorageRequest request{};check(app.storage_.inbox().receive(request) && request.kind==MessageKind::StopWifi,"Wi-Fi stop command uses Storage inbox");
        check(app.storage_.requestWifiRegenerate() && app.storage_.inbox().receive(request) && request.kind==MessageKind::RegenerateWifi,"renewal uses the same one-slot Storage inbox");
    }
    {
        struct SmallWifi {int marker=123;char retained[29]{};};
        BootModeResources<GpsProbe,UsbProbe,SmallWifi> memory;
        check(!memory.wifiHeapTail().bytes(),"unselected mode cannot donate memory");
        check(memory.selectWifi(),"select small Wi-Fi member");
        const auto tail=memory.wifiHeapTail();
        const auto start=reinterpret_cast<uintptr_t>(&memory.data_);
        check(tail.bytes() && !(tail.begin&15) && !(tail.end&15),"tail is nonempty and 16-byte aligned");
        check(tail.begin>=start+sizeof(SmallWifi) && tail.end<=start+sizeof(memory.data_),"tail excludes live prefix and the mode tag");
        std::memset(reinterpret_cast<void*>(tail.begin),0xa5,tail.bytes());
        check(memory.wifi().marker==123 && memory.isWifi(),"allocator writes cannot damage the live Wi-Fi member or tag");
        memory.sealWifiHeapTail();check(!memory.wifiHeapTail().bytes(),"tail cannot be registered twice");
        memory.destroyForShutdown();
        check(!memory.selectGps(1) && !memory.selectUsb(1) && !memory.selectWifi(),"donation permanently forbids reconstructing any mode before reboot");
    }
    std::printf("MODE SIZES host: app_gps=%zu app_usb=%zu gps_mode=%zu storage_gps=%zu storage_usb=%zu mode_union=%zu Application=%zu\n",
        sizeof(AppGpsResources),sizeof(AppUsbResources),sizeof(GpsApplicationResources),
        sizeof(StorageGpsResources),sizeof(StorageUsbResources),sizeof(ApplicationModeMemory),sizeof(Application));
    std::puts("PASS: empty startup, one-time mode construction, shared addresses, opposite-mode rejection, active-only destruction and stable GPS loans");
}

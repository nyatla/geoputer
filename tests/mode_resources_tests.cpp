#include "ApplicationRuntime.h"
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
void GpsUiTask::run(){}void GpsTask::run(){}void GpsStorageTask::run(){}
void UsbUiTask::run(){}void UsbStorageTask::run(){}
void UsbStorageTask::sdBegin(){}void UsbStorageTask::sdEnd(){}
void WifiUiTask::run(){}void WifiStorageTask::run(){}
void WifiStorageTask::sdBegin(){}void WifiStorageTask::sdEnd(){}
void GpsStorageTask::sdBegin(){}void GpsStorageTask::sdEnd(){}void GpsStorageTask::cacheState(RgCacheState,bool){}
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
        ApplicationMemory apps;check(!apps.selected(),"no app constructed at static initialization");
        check(apps.selectUsb() && !apps.selectGps(),"USB is an independent whole application");
        auto& usb=apps.usb();check(&usb.ui_.events_==&usb.events_ && &usb.storage_.events_==&usb.events_,"USB peers use only USB queues");
        check(std::holds_alternative<LogReader>(usb.storage_.work_),"USB never constructs a GPS map job");
        check(sizeof(UsbUiTask)<sizeof(GpsUiTask) && sizeof(UsbChannels::UiRequest)<sizeof(UiRequest),"USB has no GPS images or GPS payload");
    }
    {
        ApplicationMemory apps;check(apps.selectGps(),"GPS whole application");auto& app=apps.gps();
        auto& gps=app.storage_.gpsState();
        check(reinterpret_cast<uintptr_t>(&gps)>reinterpret_cast<uintptr_t>(&app.storage_.state_),"Storage observer state outlives reader/writer resources during destruction");
        check(&gps.selected_dataset_==&gps.place_lookup_.selected_,"lookup borrows Storage's selected dataset");
        check(gps.screen_resources_.live_map.pixels==gps.code_images_.map.image().data() &&
              gps.screen_resources_.log_map.pixels==gps.code_images_.map.image().data(),"map sessions use a stable GPS raster");
        check(&app.storage_.work()==&gps.screen_resources_.work,"Reader workspace remains Storage-owned");
        check(&app.ui_.notifications_==&app.notifications_ && &app.gps_.notifications_==&app.notifications_,"all GPS tasks belong to the GPS app");
    }
    {
        ApplicationMemory apps;check(apps.selectWifi() && !apps.selectUsb(),"independent Wi-Fi application");auto& app=apps.wifi();
        check(&app.ui_.events_==&app.events_ && &app.storage_.events_==&app.events_,"Wi-Fi uses its own typed queues");
        check(sizeof(WifiUiTask)<sizeof(GpsUiTask) && sizeof(WifiChannels::UiRequest)<sizeof(UiRequest),"Wi-Fi has no GPS image or payload");
        const auto tail=apps.wifiHeapTail();
        check(tail.begin>=reinterpret_cast<uintptr_t>(&app)+sizeof(app),"donation excludes all Wi-Fi tasks and queues");
        check(tail.end<=reinterpret_cast<uintptr_t>(&apps.kind_),"donation excludes the owner tag");
        std::memset(reinterpret_cast<void*>(tail.begin),0xa5,tail.bytes());
        check(apps.isWifi() && !app.ui_.model_.pending(),"allocator may write unused tail without damaging the live application");
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
    std::printf("MODE SIZES host: gps=%zu usb=%zu wifi=%zu union=%zu runtime=%zu\n",sizeof(GpsApplication),sizeof(UsbApplication),sizeof(WifiApplication),sizeof(ApplicationMemory),sizeof(ApplicationRuntime));
    std::puts("PASS: empty startup, one-time mode construction, shared addresses, opposite-mode rejection, active-only destruction and stable GPS loans");
}

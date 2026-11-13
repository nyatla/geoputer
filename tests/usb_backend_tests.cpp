#include "task/storage/usb/UsbStorage.h"
#include "BootMode.h"
#include <USB.h>
#include <ff.h>
#include <Preferences.h>
#include <future>
#include <thread>
#include <cstdio>
#include <stdexcept>
using namespace geoputer;
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
int main() {
    try {
        SPIClass spi;UsbStorage backend;backend.registerInterface();
        HostUSB::present=HostUSB::mounted=true;
        check(backend.begin(spi),"raw SD init");
        auto& msc=*USBMSC::instance;
        uint8_t sample[512]{};
        check(msc.read(0,0,sample,512)==512,"PC read for activity");
        auto activity=backend.takeActivity();check(activity.accesses==1 && !activity.active,"finished PC read is retained as one pulse");
        check(!backend.takeActivity().accesses,"activity count is consumed once");
        check(msc.write(0,0,sample,512)==512,"PC write for activity");
        check(backend.takeActivity().accesses==1,"PC write has the same activity path");
        uint8_t bulk[1536]{};
        const auto before_bulk_reads=usb_test::reads;
        check(msc.read(0,0,bulk,sizeof(bulk))==sizeof(bulk) &&
              usb_test::reads==before_bulk_reads+1 && backend.takeActivity().accesses==1,
              "aligned USB read reaches the SD driver as one multi-sector operation");
        std::memset(bulk,0x5c,sizeof(bulk));
        const auto before_bulk_writes=usb_test::writes;
        check(msc.write(0,0,bulk,sizeof(bulk))==sizeof(bulk) &&
              usb_test::writes==before_bulk_writes+1 && backend.takeActivity().accesses==1 &&
              !std::memcmp(usb_test::disk.data(),bulk,sizeof(bulk)),
              "aligned USB write reaches the SD driver as one multi-sector operation");
        check(msc.read(3,0,sample,0)==0 && !backend.takeActivity().accesses,"empty request never lights the SD icon");
        check(!backend.takeLocal() && !usb_test::mounts,"mounted PC cannot be bypassed by a local request");
        check(saveBootMode(BootMode::UsbStorage),"persist USB mode before ownership tests");
        check(msc.start_stop(0,false,true) && backend.snapshot().ejected,"safe removal is acknowledged after SD sync");
        check(backend.takeLocal() && backend.local() && usb_test::registered && !msc.media,"local mount follows detach");
        uint8_t block[512]{};
        const auto reads=usb_test::reads;
        check(msc.read(0,0,block,512)==-1 && usb_test::reads==reads && !backend.snapshot().io_error,
              "stray raw reads never touch a locally mounted filesystem");
        check(!backend.takeActivity().accesses,"locally owned raw request has no activity");
        HostUSB::mounted=true;
        check(!msc.start_stop(0,true,true) && !msc.media,"reconnect/load cannot expose a locally owned card");
        check(!backend.exitToLogger(),"local ownership cannot reboot before closing its files");
        check(backend.publish() && !backend.local() && !usb_test::registered && msc.media,"publish closes the VFS ownership period");
        check(msc.start_stop(0,false,true) && backend.takeLocal(),"second export can mount again without stale VFS registration");
        usb_test::sync_error=true;
        check(!backend.publish() && backend.local() && !msc.media,"failed sync retains local ownership and blocks raw exposure");
        usb_test::sync_error=false;
        check(backend.publish(),"retry sync can safely republish");
        // A reader already inside the mutex must finish before the local mount.
        std::promise<void> entered,release;
        auto permission=release.get_future().share();
        usb_test::on_read=[&]{entered.set_value();permission.wait();};
        auto read=std::async(std::launch::async,[&]{return msc.read(0,0,block,512);});
        entered.get_future().wait();
        activity=backend.takeActivity();check(activity.accesses==1 && activity.active,"in-flight PC IO remains active after the pulse is consumed");
        HostUSB::mounted=false;
        auto local=std::async(std::launch::async,[&]{return backend.takeLocal();});
        const bool waited=local.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout;
        release.set_value();check(read.get()==512,"in-flight read completes");check(local.get() && waited,"local mount waits for in-flight raw IO");
        usb_test::on_read={};check(backend.publish(),"publish after inflight handoff");
        check(msc.start_stop(0,false,true) && backend.takeLocal(),"local ownership before raw reinitialization failure");
        usb_test::init_error=true;
        check(!backend.publish() && !backend.local() && !backend.snapshot().ready && !msc.media && !usb_test::registered,
              "raw init failure after unmount exposes no medium and releases local ownership");
        check(backend.exitToLogger(),"failed republish can still return to logger without an unsafe mount");
        BootMode mode;check(loadBootMode(mode) && mode==BootMode::GpsLogger,"successful exit persists GPS mode");
        // A confirmed device-side exit does not require a host eject, but it
        // still drains active IO, syncs and saves the flag before releasing SD.
        usb_test::init_error=false;HostUSB::mounted=true;
        UsbStorage exiting;check(exiting.begin(spi) && saveBootMode(BootMode::UsbStorage),"prepare connected exit");
        usb_test::sync_error=true;
        check(!exiting.exitToLogger() && exiting.snapshot().ready && msc.media &&
              loadBootMode(mode) && mode==BootMode::UsbStorage,"failed exit sync retains USB mode and medium");
        usb_test::sync_error=false;
        check(msc.read(0,0,block,512)==512,"failed sync restores raw IO for retry");
        Preferences::fail_write=true;
        check(!exiting.exitToLogger() && exiting.snapshot().ready && msc.media &&
              loadBootMode(mode) && mode==BootMode::UsbStorage,"failed flag write retains USB mode and medium");
        Preferences::fail_write=false;
        check(msc.read(0,0,block,512)==512,"failed flag write restores raw IO for retry");
        std::promise<void> exit_entered,exit_release;
        auto exit_permission=exit_release.get_future().share();
        usb_test::on_read=[&]{exit_entered.set_value();exit_permission.wait();};
        auto exit_read=std::async(std::launch::async,[&]{return msc.read(0,0,block,512);});
        exit_entered.get_future().wait();
        auto exit_result=std::async(std::launch::async,[&]{return exiting.exitToLogger();});
        const bool exit_waited=exit_result.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout;
        exit_release.set_value();check(exit_read.get()==512 && exit_result.get() && exit_waited,
              "connected exit waits for the in-flight transfer without requiring eject");
        usb_test::on_read={};const auto after_exit_reads=usb_test::reads;
        check(!msc.media && !exiting.snapshot().ready && !usb_test::driver &&
              msc.read(0,0,block,512)==-1 && usb_test::reads==after_exit_reads,
              "confirmed exit blocks subsequent IO before the logger can mount SD");
        check(loadBootMode(mode) && mode==BootMode::GpsLogger,"connected exit persists GPS boot mode");
        std::puts("PASS: USB ownership, repeat mounts, reconnect gating, sync retry, inflight IO drain and device-side exit");
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}

#include "UsbStorage.h"
#include "UsbBlockIo.h"
#include "../../../BootMode.h"
#include <sd_diskio.h>
#include <ff.h>
#include <diskio.h>
#include <USB.h>
#include <tusb.h>
#include "../../../Config.h"

namespace geoputer {
USBMSC& UsbStorage::registerInterface() {
    static USBMSC interface;
    return interface;
}
bool UsbStorage::begin(SPIClass& spi) {
    spi_=&spi;
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_control_);
    if (!mutex_) { error_ = true; return false; }
    active_ = this;
    // The interface is already registered by setup; keep the medium absent
    // until the raw driver and callbacks are fully initialized.
    msc_ = &registerInterface();
    msc_->vendorID("GeoPuter"); msc_->productID("SD Card"); msc_->productRevision("1.0");
    msc_->onRead(read); msc_->onWrite(write); msc_->onStartStop(startStop);
    drive_ = sdcard_init(12, &spi, config::SdSpiHz);
    if (drive_ == 0xff || disk_initialize(drive_) != 0 || !(sectors_ = sdcard_num_sectors(drive_))) {
        error_ = true; return false;
    }
    ready_ = msc_->begin(sectors_, 512);
    msc_->mediaPresent(ready_);
    error_ = !ready_;
    return ready_;
}
UsbStorageView UsbStorage::snapshot() const {
    return {ready_,ejected_,error_,static_cast<bool>(USB),local_};
}
bool UsbStorage::takeLocal() {
    if(!mutex_)return false;
    xSemaphoreTake(mutex_,portMAX_DELAY);
    if(mounted_) {xSemaphoreGive(mutex_);return true;}
    if(!ready_ || stopping_ || usbHostNeedsEject(snapshot())) {xSemaphoreGive(mutex_);return false;}
    local_=true;msc_->mediaPresent(false);
    xSemaphoreGive(mutex_);
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(150));
    // Raw callbacks are now gated; only this Storage task can access FatFs.
    mounted_=sdcard_mount(drive_,"/sd",config::SdMaxOpenFiles,false);
    char volume[]={static_cast<char>('0'+drive_),':',0};
    FATFS* fs=nullptr;DWORD free=0;
    const bool ok=mounted_ && f_getfree(volume,&free,&fs)==FR_OK && fs && fs->fs_type==FS_FAT32;
    error_=!ok;
    return ok;
}
bool UsbStorage::publish() {
    if(!mutex_)return false;
    xSemaphoreTake(mutex_,portMAX_DELAY);
    if(!local_) {const bool ok=ready_ && !stopping_;xSemaphoreGive(mutex_);return ok;}
    bool ok=true;
    if(mounted_) {
        char volume[]={static_cast<char>('0'+drive_),':',0};
        ok=disk_ioctl(drive_,CTRL_SYNC,nullptr)==RES_OK && f_mount(nullptr,volume,0)==FR_OK;
        if(ok) {ok=sdcard_unmount(drive_)==0;mounted_=false;}
    }
    // sdcard_unmount does not unregister the VFS. Releasing the raw driver
    // also releases that registration, so another export can mount /sd again.
    if(ok) {
        if(drive_!=0xff)sdcard_uninit(drive_);
        drive_=sdcard_init(12,spi_,config::SdSpiHz);
        ok=drive_!=0xff && disk_initialize(drive_)==0 && disk_ioctl(drive_,CTRL_SYNC,nullptr)==RES_OK;
    }
    if(ok) {
        sectors_=sdcard_num_sectors(drive_);
        ok=sectors_ && msc_->begin(sectors_,512);
    }
    if(ok) {local_=false;ejected_=false;ready_=true;error_=false;msc_->mediaPresent(true);}
    else {
        error_=true;
        if(!mounted_) {ready_=false;local_=false;msc_->mediaPresent(false);}
    }
    xSemaphoreGive(mutex_);
    if(!local_)tud_connect();
    return ok;
}
int32_t UsbStorage::read(uint32_t lba, uint32_t offset, void* data, uint32_t size) {
    return active_ ? active_->transfer(lba, offset, data, size, false) : -1;
}
int32_t UsbStorage::write(uint32_t lba, uint32_t offset, uint8_t* data, uint32_t size) {
    return active_ ? active_->transfer(lba, offset, data, size, true) : -1;
}
int32_t UsbStorage::transfer(uint32_t lba, uint32_t offset, void* data, uint32_t size, bool writing) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    int32_t result = -1;
    if (ready_ && !ejected_ && !stopping_ && !local_)
        result = transferUsbBlocks(sectors_, lba, offset, data, size, writing,
            [&](uint8_t* p, uint32_t sector) { return sectorIo([&] {return sd_read_raw(drive_, p, sector);}); },
            [&](const uint8_t* p, uint32_t sector) { return sectorIo([&] {return sd_write_raw(drive_, const_cast<uint8_t*>(p), sector);}); }, scratch_);
    if (result < 0 && !local_ && !ejected_ && !stopping_) error_ = true;
    xSemaphoreGive(mutex_);
    return result;
}
bool UsbStorage::startStop(uint8_t, bool start, bool eject) {
    if (!active_ || !eject) return true;
    auto& self = *active_;
    xSemaphoreTake(self.mutex_, portMAX_DELAY);
    bool ok = !self.stopping_ && !self.local_;
    if (ok && !start && self.ready_) ok = disk_ioctl(self.drive_, CTRL_SYNC, nullptr) == RES_OK;
    if (ok) {
        self.ejected_ = !start;
        self.msc_->mediaPresent(start && self.ready_);
    } else if(!self.local_) self.error_ = true;
    xSemaphoreGive(self.mutex_);
    return ok;
}
bool UsbStorage::exitToLogger() {
    if (!mutex_) return saveBootMode(BootMode::GpsLogger);
    xSemaphoreTake(mutex_, portMAX_DELAY); // Finish the in-flight SD transfer first.
    if(local_) {xSemaphoreGive(mutex_);return false;}
    // Explicitly confirmed device-side exit: the caller has finished PC saves.
    // The mutex drains any in-flight sector IO; gate all later requests before
    // synchronizing. This cannot flush data that the PC has not sent yet.
    stopping_ = true;
    bool ok = !ready_ || disk_ioctl(drive_, CTRL_SYNC, nullptr) == RES_OK;
    if (!ok) error_ = true;
    if (ok) ok = saveBootMode(BootMode::GpsLogger);
    if (ok) {
        if (msc_) msc_->mediaPresent(false);
        ready_ = false;
        if (drive_ != 0xff) { sdcard_uninit(drive_); drive_ = 0xff; }
    }
    if (!ok) stopping_ = false; // Preserve the current mode and allow retry.
    xSemaphoreGive(mutex_);
    return ok;
}
}

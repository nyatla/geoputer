#include "StorageCard.h"
#if defined(ARDUINO_ARCH_ESP32) && !defined(GEOPUTER_TEST_FATFS)
#include "StorageFs.h"
#include <diskio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>

namespace geoputer {
namespace {
struct CardContext {
    StaticSemaphore_t mutex_control{};
    SemaphoreHandle_t mutex=nullptr;
    SD_FATFS_FATFS* fs=nullptr;
    uint8_t drive=0xff;
    bool bound=false,registered=false;
};
CardContext contexts[SD_FATFS_FF_VOLUMES];
constexpr const char* MountPath="/sd";
CardContext* card(uint8_t drive) {return drive<SD_FATFS_FF_VOLUMES?&contexts[drive]:nullptr;}
void volumeName(uint8_t drive,char (&name)[3]) {name[0]='0'+drive;name[1]=':';name[2]=0;}
bool bind(CardContext& c,uint8_t drive) {
    if(c.bound)return false;
    c.drive=drive;
    if(!c.mutex)c.mutex=xSemaphoreCreateMutexStatic(&c.mutex_control);
    if(!c.mutex)return false;
    SD_FATFS_Device device{
        &c,
        [](void* p)->SD_FATFS_DSTATUS{return ::disk_initialize(static_cast<CardContext*>(p)->drive);},
        [](void* p)->SD_FATFS_DSTATUS{return ::disk_status(static_cast<CardContext*>(p)->drive);},
        [](void* p,uint8_t* data,uint32_t sector,unsigned count){return SD_FATFS_DRESULT(::disk_read(static_cast<CardContext*>(p)->drive,data,sector,count));},
        [](void* p,const uint8_t* data,uint32_t sector,unsigned count){return SD_FATFS_DRESULT(::disk_write(static_cast<CardContext*>(p)->drive,data,sector,count));},
        [](void* p,uint8_t command,void* data){return SD_FATFS_DRESULT(::disk_ioctl(static_cast<CardContext*>(p)->drive,command,data));},
        [](void* p,uint32_t timeout)->int{return xSemaphoreTake(static_cast<CardContext*>(p)->mutex,pdMS_TO_TICKS(timeout))==pdTRUE;},
        [](void* p){xSemaphoreGive(static_cast<CardContext*>(p)->mutex);}};
    if(sd_fatfs_bind_device(drive,&device)!=SD_FATFS_FR_OK)return false;
    SD_FATFS_VolToPart[drive]={drive,0};
    sd_fatfs_set_clock([](void*)->uint32_t{return ::get_fattime();},nullptr);
    return c.bound=true;
}
}
uint8_t initStorageCard(uint8_t cs,SPIClass* spi,int hz) {
    const auto drive=::sdcard_init(cs,spi,hz);
    if(drive==0xff)return drive;
    auto* c=card(drive);
    if(c && bind(*c,drive))return drive;
    ::sdcard_uninit(drive);return 0xff;
}
bool mountStorageCard(uint8_t drive,const char* path,uint8_t max_files,bool format_if_empty) {
    auto* c=card(drive);
    if(!c || !c->bound || !path || std::strcmp(path,MountPath) || format_if_empty)return false;
    char volume[3];volumeName(drive,volume);
    if(!c->registered) {
        if(sd_fatfs_vfs_register(MountPath,volume,max_files,&c->fs)!=ESP_OK)return false;
        c->registered=true;
    }
    if(sd_fatfs_f_mount(c->fs,volume,1)==SD_FATFS_FR_OK)return true;
    (void)sd_fatfs_f_mount(nullptr,volume,0);
    if(sd_fatfs_vfs_unregister_path(MountPath)==ESP_OK){c->registered=false;c->fs=nullptr;}
    return false;
}
uint8_t unmountStorageCard(uint8_t drive) {
    auto* c=card(drive);if(!c || !c->bound)return 1;
    char volume[3];volumeName(drive,volume);
    return sd_fatfs_f_mount(nullptr,volume,0)==SD_FATFS_FR_OK?0:1;
}
uint8_t releaseStorageCard(uint8_t drive) {
    auto* c=card(drive);if(!c || !c->bound)return 1;
    if(unmountStorageCard(drive))return 1;
    if(c->registered) {
        if(sd_fatfs_vfs_unregister_path(MountPath)!=ESP_OK)return 1;
        c->registered=false;c->fs=nullptr;
    }
    if(sd_fatfs_unbind_device(drive)!=SD_FATFS_FR_OK)return 1;
    c->bound=false;
    return ::sdcard_uninit(drive);
}
}
#endif

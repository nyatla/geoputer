// Read-only, alternating SDK-FatFs / SD_FatFs comparison for Cardputer ADV.
// No VFS, NVS, GPS or application tasks run. Only one FatFs mounts the SD.
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <sd_diskio.h>
#include <USB.h>
#include <USBCDC.h>
#include <ff.h>
#include <diskio.h>
#include <SD_FatFs.h>
#include <esp_timer.h>
#include <esp_rom_crc.h>
#include <driver/gpio.h>
#include <algorithm>
#include <array>
#include <cstring>

USBCDC console;
static SPIClass spi(FSPI);
static uint8_t physical_drive=0xff;
static FATFS sdk_fs;
static SD_FATFS_FATFS sd_fs;
static FIL sdk_file;
static SD_FATFS_FIL sd_file;
static SemaphoreHandle_t sd_mutex;
static uint8_t data[4096];
static uint32_t read_calls=0,read_sectors=0,write_calls=0;
static uint64_t physical_us=0;
extern "C" DRESULT __real_ff_disk_read(BYTE,BYTE*,DWORD,UINT);
extern "C" DRESULT __wrap_ff_disk_read(BYTE drive,BYTE* buffer,DWORD sector,UINT count) {
    const auto start=esp_timer_get_time();
    const auto result=__real_ff_disk_read(drive,buffer,sector,count);
    physical_us+=esp_timer_get_time()-start;++read_calls;read_sectors+=count;
    return result;
}
extern "C" DRESULT __wrap_ff_disk_write(BYTE,const BYTE*,DWORD,UINT) {
    ++write_calls;return RES_WRPRT; // Fail closed: this entire benchmark is read-only.
}
static bool check(bool ok,const char* operation) {
    if(!ok)console.printf("[SD-AB-ERROR] %s\n",operation);
    return ok;
}
static bool mount(bool dedicated) {
    return check(dedicated?sd_fatfs_f_mount(&sd_fs,"0:",1)==SD_FATFS_FR_OK
                          :f_mount(&sdk_fs,"0:",1)==FR_OK,"mount");
}
static bool unmount(bool dedicated) {
    return check(dedicated?sd_fatfs_f_mount(nullptr,"0:",0)==SD_FATFS_FR_OK
                          :f_mount(nullptr,"0:",0)==FR_OK,"unmount");
}
struct FileInfo {char path[512]{};uint32_t bytes=0;};
static std::array<FileInfo,12> files;
static size_t file_count=0;
static bool discover() {
    if(!mount(false))return false;
    for(unsigned directory=1;directory<=2;++directory) {
        char path[32];snprintf(path,sizeof(path),"0:/dataset/%02u",directory);
        FF_DIR dir{};
        if(f_opendir(&dir,path)!=FR_OK)continue;
        for(;;) {
            FILINFO info{};
            if(!check(f_readdir(&dir,&info)==FR_OK,"readdir"))return false;
            if(!info.fname[0])break;
            if(info.fattrib&AM_DIR)continue;
            if(!strstr(info.fname,".glc") && !strstr(info.fname,".giswordbook") && !strstr(info.fname,".dat"))continue;
            if(file_count==files.size())break;
            auto& file=files[file_count++];file.bytes=info.fsize;
            if(!check(snprintf(file.path,sizeof(file.path),"%s/%s",path,info.fname)<int(sizeof(file.path)),"path"))return false;
            console.printf("[SD-AB-FILE] id=%u bytes=%lu path=%s\n",unsigned(file_count-1),(unsigned long)file.bytes,file.path);
        }
        if(!check(f_closedir(&dir)==FR_OK,"closedir"))return false;
    }
    if(!unmount(false))return false;
    return check(file_count>0,"no dataset files");
}
struct Result {uint64_t io=0,total=0,physical=0;uint32_t crc=0,bytes=0,calls=0,sectors=0;};
static bool measure(bool dedicated,const FileInfo& source,unsigned pattern,Result& result) {
    if(!mount(dedicated))return false;
    read_calls=read_sectors=0;physical_us=0;
    auto start=esp_timer_get_time();auto call=start;
    bool ok=dedicated?sd_fatfs_f_open(&sd_file,source.path,SD_FATFS_FA_READ)==SD_FATFS_FR_OK
                     :f_open(&sdk_file,source.path,FA_READ)==FR_OK;
    result.io+=esp_timer_get_time()-call;
    if(!check(ok,"open"))return false;
    auto seek=[&](uint32_t position) {
        const auto t=esp_timer_get_time();
        const bool good=dedicated?sd_fatfs_f_lseek(&sd_file,position)==SD_FATFS_FR_OK && sd_fatfs_f_tell(&sd_file)==position
                                 :f_lseek(&sdk_file,position)==FR_OK && f_tell(&sdk_file)==position;
        result.io+=esp_timer_get_time()-t;return good;
    };
    auto read=[&](unsigned count) {
        const auto t=esp_timer_get_time();unsigned received=0;
        const bool good=dedicated?sd_fatfs_f_read(&sd_file,data,count,&received)==SD_FATFS_FR_OK
                                 :f_read(&sdk_file,data,count,&received)==FR_OK;
        result.io+=esp_timer_get_time()-t;
        if(!good || received!=count)return false;
        result.crc=esp_rom_crc32_le(result.crc,data,count);result.bytes+=count;return true;
    };
    if(pattern<=2) {
        const uint32_t offset=pattern==2?1:0;
        const uint32_t bytes=std::min<uint32_t>(512*1024,source.bytes-offset);
        uint32_t remaining=bytes;
        ok=seek(offset);
        const unsigned block=pattern==1?512:4096;
        while(ok && remaining) {const unsigned count=std::min<uint32_t>(block,remaining);ok=read(count);remaining-=count;}
    }else if(pattern==3) {
        uint32_t random=0x12345678;
        const unsigned count=std::min<uint32_t>(4096,source.bytes);
        for(unsigned n=0;ok && n<32;++n) {
            random^=random<<13;random^=random>>17;random^=random<<5;
            ok=seek(random%(source.bytes-count+1)) && read(count);
        }
    }else if(pattern==4) {
        const uint32_t limit=std::min<uint32_t>(512,source.bytes);
        for(unsigned n=0;ok && n<512;++n)ok=seek((n*137)%limit) && read(1);
    }else {
        const uint32_t limit=std::min<uint32_t>(128,(source.bytes-17)/512);
        for(unsigned n=0;ok && n<limit;++n)ok=seek(n*512+509) && read(17);
    }
    call=esp_timer_get_time();
    const bool closed=dedicated?sd_fatfs_f_close(&sd_file)==SD_FATFS_FR_OK:f_close(&sdk_file)==FR_OK;
    result.io+=esp_timer_get_time()-call;
    result.total=esp_timer_get_time()-start;result.physical=physical_us;result.calls=read_calls;result.sectors=read_sectors;
    const bool unmounted=unmount(dedicated);
    return check(ok && closed && unmounted,"read/seek/close");
}
static bool run() {
    // Keep the unused MeshKit radio in reset, including held pins from last boot.
    digitalWrite(3,LOW);pinMode(3,OUTPUT);gpio_hold_dis(GPIO_NUM_3);gpio_hold_en(GPIO_NUM_3);
    digitalWrite(5,HIGH);pinMode(5,OUTPUT);gpio_hold_en(GPIO_NUM_5);
    spi.begin(40,39,14,12);
    physical_drive=sdcard_init(12,&spi,25000000);
    if(!check(physical_drive!=0xff,"sdcard_init"))return false;
    VolToPart[0]={physical_drive,0};
    sd_mutex=xSemaphoreCreateMutex();if(!check(sd_mutex!=nullptr,"mutex"))return false;
    SD_FATFS_Device backend{
        nullptr,
        [](void*)->SD_FATFS_DSTATUS{return disk_initialize(physical_drive);},
        [](void*)->SD_FATFS_DSTATUS{return disk_status(physical_drive);},
        [](void*,uint8_t* out,uint32_t sector,unsigned count){return SD_FATFS_DRESULT(disk_read(physical_drive,out,sector,count));},
        [](void*,const uint8_t*,uint32_t,unsigned){++write_calls;return SD_FATFS_RES_WRPRT;},
        [](void*,uint8_t command,void* out){return SD_FATFS_DRESULT(disk_ioctl(physical_drive,command,out));},
        [](void*,uint32_t timeout)->int{return xSemaphoreTake(sd_mutex,pdMS_TO_TICKS(timeout))==pdTRUE;},
        [](void*){xSemaphoreGive(sd_mutex);}};
    if(!check(sd_fatfs_bind_device(0,&backend)==SD_FATFS_FR_OK,"bind"))return false;
    if(!discover())return false;
    console.printf("[SD-AB-BEGIN] sdk_fil=%u sd_fil=%u sdk_fatfs=%u sd_fatfs=%u hz=%lu readonly=1\n",
        unsigned(sizeof(FIL)),unsigned(sizeof(SD_FATFS_FIL)),unsigned(sizeof(FATFS)),unsigned(sizeof(SD_FATFS_FATFS)),
        (unsigned long)spiClockDivToFrequency(spi.getClockDivider()));
    const char* patterns[]={"seq4k","seq512","unaligned4k","random4k","cached_byte","boundary17"};
    unsigned cases=0;
    for(size_t file=0;file<file_count;++file)for(unsigned pattern=0;pattern<6;++pattern) {
        Result reference{};
        for(unsigned round=0;round<5;++round)for(unsigned order=0;order<2;++order) {
            const bool dedicated=(order^(round&1))!=0;
            Result result{};
            if(!measure(dedicated,files[file],pattern,result))return false;
            if(round==0 && order==0)reference=result;
            if(!check(reference.crc==result.crc && reference.bytes==result.bytes && reference.calls==result.calls && reference.sectors==result.sectors,"data/physical-I/O mismatch"))return false;
            ++cases;
            console.printf("[SD-AB] file=%u pattern=%s round=%u mode=%s io_us=%llu total_us=%llu physical_us=%llu bytes=%lu crc=%08lx reads=%lu sectors=%lu writes=%lu\n",
                unsigned(file),patterns[pattern],round,dedicated?"sd512":"sdk4096",(unsigned long long)result.io,(unsigned long long)result.total,
                (unsigned long long)result.physical,(unsigned long)result.bytes,(unsigned long)result.crc,(unsigned long)result.calls,
                (unsigned long)result.sectors,(unsigned long)write_calls);
            delay(5);
        }
    }
    if(!check(write_calls==0,"write attempts"))return false;
    if(!check(sd_fatfs_unbind_device(0)==SD_FATFS_FR_OK,"unbind"))return false;
    sdcard_uninit(physical_drive);
    console.printf("[SD-AB-DONE] OK files=%u cases=%u writes=%lu\n",unsigned(file_count),cases,(unsigned long)write_calls);
    return true;
}
void setup() {
    // Apply reset protection before USB startup as in the normal firmware.
    digitalWrite(3,LOW);pinMode(3,OUTPUT);gpio_hold_dis(GPIO_NUM_3);gpio_hold_en(GPIO_NUM_3);
    digitalWrite(5,HIGH);pinMode(5,OUTPUT);
    console.begin(115200);USB.productName("ReverseGeoputer SD benchmark");USB.begin();
    const auto started=millis();while(!console && millis()-started<15000)delay(20);
    delay(2000);
    if(!run())console.println("[SD-AB-DONE] FAILED");
}
void loop(){delay(1000);}

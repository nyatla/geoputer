#ifdef SD_FIRST
#include <SD_FatFs.h>
#endif
#include "ff.h"
#include "diskio.h"
#ifndef SD_FIRST
#include <SD_FatFs.h>
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <mutex>
#include <type_traits>
#include <vector>

static_assert(FF_MAX_SS == 4096, "SDK configuration was changed");
static_assert(SD_FATFS_FF_MAX_SS == 512, "SD sector size");
static_assert(!std::is_same<FIL, SD_FATFS_FIL>::value, "FIL types must be distinct");
static_assert(!std::is_convertible<FIL*, SD_FATFS_FIL*>::value, "No cross-library handles");
static_assert(sizeof(((FIL*)nullptr)->buf) == 4096, "SDK file buffer");
static_assert(sizeof(((SD_FATFS_FIL*)nullptr)->buf) == 512, "SD file buffer");

struct Disk {
    std::vector<uint8_t> bytes;
    uint16_t sector_size;
    std::mutex mutex;
    bool protected_media = false;
    bool read_error = false;
    bool lock_timeout = false;
    mutable unsigned status_calls = 0;
    unsigned locks = 0;
    unsigned unlocks = 0;
    Disk(size_t size, uint16_t sector):bytes(size),sector_size(sector) {}
    uint8_t status() const {++status_calls;return protected_media ? STA_PROTECT : 0;}
    int read(uint8_t* out, uint32_t sector, unsigned count) {
        if (read_error) return RES_ERROR;
        const uint64_t start=uint64_t(sector)*sector_size, length=uint64_t(count)*sector_size;
        if (start>bytes.size() || length>bytes.size()-start) return RES_PARERR;
        std::memcpy(out,bytes.data()+start,length);return RES_OK;
    }
    int write(const uint8_t* in, uint32_t sector, unsigned count) {
        if (protected_media) return RES_WRPRT;
        const uint64_t start=uint64_t(sector)*sector_size, length=uint64_t(count)*sector_size;
        if (start>bytes.size() || length>bytes.size()-start) return RES_PARERR;
        std::memcpy(bytes.data()+start,in,length);return RES_OK;
    }
    int ioctl(uint8_t command, void* value) {
        switch (command) {
        case CTRL_SYNC:return RES_OK;
        case GET_SECTOR_COUNT:*static_cast<uint32_t*>(value)=bytes.size()/sector_size;return RES_OK;
        case GET_SECTOR_SIZE:*static_cast<uint16_t*>(value)=sector_size;return RES_OK;
        case GET_BLOCK_SIZE:*static_cast<uint32_t*>(value)=1;return RES_OK;
        default:return RES_PARERR;
        }
    }
};
static Disk standard_disk(16*1024*1024,4096);
extern "C" {
PARTITION VolToPart[FF_VOLUMES]={{0,0},{1,0}};
DSTATUS disk_initialize(BYTE p) {return p==0 ? standard_disk.status():STA_NOINIT;}
DSTATUS disk_status(BYTE p) {return disk_initialize(p);}
DRESULT disk_read(BYTE p,BYTE* b,DWORD s,UINT n) {return p==0 ? DRESULT(standard_disk.read(b,s,n)):RES_PARERR;}
DRESULT disk_write(BYTE p,const BYTE* b,DWORD s,UINT n) {return p==0 ? DRESULT(standard_disk.write(b,s,n)):RES_PARERR;}
DRESULT disk_ioctl(BYTE p,BYTE c,void* b) {return p==0 ? DRESULT(standard_disk.ioctl(c,b)):RES_PARERR;}
DWORD get_fattime() {return (46u<<25)|(10u<<21)|(28u<<16);}
void ff_memfree(void*) {}
int ff_cre_syncobj(BYTE,FF_SYNC_t* out) {*out=&standard_disk.mutex;return 1;}
int ff_req_grant(FF_SYNC_t p) {static_cast<std::mutex*>(p)->lock();return 1;}
void ff_rel_grant(FF_SYNC_t p) {static_cast<std::mutex*>(p)->unlock();}
int ff_del_syncobj(FF_SYNC_t) {return 1;}
}
static SD_FATFS_Device device(Disk& disk) {
    return {&disk,
        [](void* p)->SD_FATFS_DSTATUS {return static_cast<Disk*>(p)->status();},
        [](void* p)->SD_FATFS_DSTATUS {return static_cast<Disk*>(p)->status();},
        [](void* p,uint8_t* b,uint32_t s,unsigned n) {return SD_FATFS_DRESULT(static_cast<Disk*>(p)->read(b,s,n));},
        [](void* p,const uint8_t* b,uint32_t s,unsigned n) {return SD_FATFS_DRESULT(static_cast<Disk*>(p)->write(b,s,n));},
        [](void* p,uint8_t c,void* b) {return SD_FATFS_DRESULT(static_cast<Disk*>(p)->ioctl(c,b));},
        [](void* p,uint32_t ms) {assert(ms==10000);auto& d=*static_cast<Disk*>(p);
            if(d.lock_timeout)return 0;d.mutex.lock();++d.locks;return 1;},
        [](void* p) {auto& d=*static_cast<Disk*>(p);++d.unlocks;d.mutex.unlock();}};
}
int main() {
    Disk sd(64*1024*1024,512);
    auto backend=device(sd);
    assert(sd_fatfs_bind_device(0,nullptr)==SD_FATFS_FR_INVALID_PARAMETER);
    assert(sd_fatfs_bind_device(2,&backend)==SD_FATFS_FR_INVALID_DRIVE);
    assert(sd_fatfs_bind_device(0,&backend)==SD_FATFS_FR_OK);
    assert(sd_fatfs_bind_device(0,&backend)==SD_FATFS_FR_EXIST);
    std::array<uint8_t,4096> workspace{};
    assert(f_mkfs("0:",FM_FAT|FM_SFD,0,workspace.data(),workspace.size())==FR_OK);
    assert(sd_fatfs_f_mkfs("0:",SD_FATFS_FM_FAT32,512,nullptr,0)==SD_FATFS_FR_NOT_ENOUGH_CORE);
    assert(sd_fatfs_f_mkfs("0:",SD_FATFS_FM_FAT32|SD_FATFS_FM_SFD,512,workspace.data(),workspace.size())==SD_FATFS_FR_OK);
    FATFS stdfs{};SD_FATFS_FATFS sdfs{};
    assert(f_mount(&stdfs,"0:",1)==FR_OK);
    assert(sd_fatfs_f_mount(&sdfs,"0:",1)==SD_FATFS_FR_OK);
    assert(stdfs.fs_type==FS_FAT12 || stdfs.fs_type==FS_FAT16);
    assert(sdfs.fs_type==SD_FATFS_FS_FAT32);
    assert(sd_fatfs_unbind_device(0)==SD_FATFS_FR_LOCKED);
    sd_fatfs_set_clock([](void*)->uint32_t {return get_fattime();},nullptr);
    assert(sd_fatfs_f_mkdir("0:/習志野市")==SD_FATFS_FR_OK);
    const char* path="0:/習志野市/UTF8_測位記録.gps.dat";
    FIL standard{};SD_FATFS_FIL dedicated{};
    assert(f_open(&standard,"0:/same.dat",FA_CREATE_ALWAYS|FA_READ|FA_WRITE)==FR_OK);
    assert(sd_fatfs_f_open(&dedicated,path,SD_FATFS_FA_CREATE_ALWAYS|SD_FATFS_FA_READ|SD_FATFS_FA_WRITE)==SD_FATFS_FR_OK);
    std::vector<uint8_t> payload(65539);
    for(size_t i=0;i<payload.size();++i)payload[i]=(i*137+19)&255;
    UINT stdn=0;SD_FATFS_UINT sdn=0;
    assert(f_write(&standard,"standard",8,&stdn)==FR_OK && stdn==8);
    assert(sd_fatfs_f_write(&dedicated,payload.data(),payload.size(),&sdn)==SD_FATFS_FR_OK && sdn==payload.size());
    assert(f_sync(&standard)==FR_OK);
    assert(sd_fatfs_f_sync(&dedicated)==SD_FATFS_FR_OK);
    assert(f_size(&standard)==8 && sd_fatfs_f_size(&dedicated)==payload.size());
    std::array<uint8_t,8195> result{};
    for(uint32_t offset:{0u,1u,509u,4093u,57001u}) {
        auto length=std::min<size_t>(result.size(),payload.size()-offset);
        assert(sd_fatfs_f_lseek(&dedicated,offset)==SD_FATFS_FR_OK);
        assert(sd_fatfs_f_read(&dedicated,result.data(),length,&sdn)==SD_FATFS_FR_OK && sdn==length);
        assert(std::memcmp(result.data(),payload.data()+offset,length)==0);
    }
    assert(f_lseek(&standard,0)==FR_OK);
    assert(f_read(&standard,result.data(),8,&stdn)==FR_OK && stdn==8 && std::memcmp(result.data(),"standard",8)==0);
    assert(f_close(&standard)==FR_OK);
    assert(sd_fatfs_f_close(&dedicated)==SD_FATFS_FR_OK);
    SD_FATFS_FILINFO info{};
    assert(sd_fatfs_f_stat("0:/same.dat",&info)==SD_FATFS_FR_NO_FILE);
    FILINFO stdinfo{};assert(f_stat("0:/習志野市",&stdinfo)==FR_NO_FILE);
    assert(sd_fatfs_f_stat(path,&info)==SD_FATFS_FR_OK && info.fsize==payload.size());
    assert(info.fdate==((46u<<9)|(10u<<5)|28u));
    SD_FATFS_FF_DIR dir{};
    assert(sd_fatfs_f_opendir(&dir,"0:/習志野市")==SD_FATFS_FR_OK);
    assert(sd_fatfs_f_readdir(&dir,&info)==SD_FATFS_FR_OK && std::strcmp(info.fname,"UTF8_測位記録.gps.dat")==0);
    assert(sd_fatfs_f_closedir(&dir)==SD_FATFS_FR_OK);
    assert(sd_fatfs_f_rename(path,"0:/習志野市/renamed.dat")==SD_FATFS_FR_OK);
    sd.lock_timeout=true;assert(sd_fatfs_f_stat("0:/習志野市/renamed.dat",&info)==SD_FATFS_FR_TIMEOUT);sd.lock_timeout=false;
    sd.protected_media=true;
    assert(sd_fatfs_f_open(&dedicated,"0:/denied.dat",SD_FATFS_FA_CREATE_ALWAYS|SD_FATFS_FA_WRITE)==SD_FATFS_FR_WRITE_PROTECTED);
    sd.protected_media=false;
    uint32_t free_clusters=0;SD_FATFS_FATFS* out=nullptr;
    assert(sd_fatfs_f_getfree("0:",&free_clusters,&out)==SD_FATFS_FR_OK && out==&sdfs && free_clusters>0);
    assert(sd_fatfs_f_unlink("0:/習志野市/renamed.dat")==SD_FATFS_FR_OK);
    assert(sd_fatfs_f_unlink("0:/習志野市")==SD_FATFS_FR_OK);
    const auto statuses=sd.status_calls;
    uint16_t sector_size=0;
    assert(sd_fatfs_disk_ioctl(0,SD_FATFS_GET_SECTOR_SIZE,&sector_size)==SD_FATFS_RES_OK && sector_size==512);
    sd.protected_media=true;
    assert(sd_fatfs_disk_write(0,result.data(),0,1)==SD_FATFS_RES_WRPRT);
    sd.protected_media=false;
    sd.read_error=true;
    assert(sd_fatfs_disk_read(0,result.data(),0,1)==SD_FATFS_RES_ERROR);sd.read_error=false;
    assert(sd.status_calls==statuses); // No redundant hardware status transactions.
    assert(sd_fatfs_disk_read(0,nullptr,0,1)==SD_FATFS_RES_PARERR);
    assert(sd_fatfs_disk_read(0,result.data(),0,0)==SD_FATFS_RES_PARERR);
    assert(sd_fatfs_disk_read(0,result.data(),sd.bytes.size()/512,1)==SD_FATFS_RES_PARERR);
    assert(sd_fatfs_f_mount(nullptr,"0:",0)==SD_FATFS_FR_OK);
    assert(sd_fatfs_unbind_device(0)==SD_FATFS_FR_OK);
    assert(sd.locks==sd.unlocks && sd.locks>0);
    assert(sd_fatfs_disk_read(0,result.data(),0,1)==SD_FATFS_RES_NOTRDY);
    // A 4096-byte-sector backend must be rejected before any sector transfer.
    auto bad=device(standard_disk);
    assert(sd_fatfs_bind_device(1,&bad)==SD_FATFS_FR_OK);
    SD_FATFS_FATFS invalid{};
    assert(sd_fatfs_f_mount(&invalid,"1:",1)==SD_FATFS_FR_NOT_READY);
    assert(sd_fatfs_unbind_device(1)==SD_FATFS_FR_LOCKED);
    assert(sd_fatfs_f_mount(nullptr,"1:",0)==SD_FATFS_FR_OK);
    assert(sd_fatfs_unbind_device(1)==SD_FATFS_FR_OK);
    assert(f_mount(nullptr,"0:",0)==FR_OK);
    std::cout<<"coexistence OK: SDK FIL="<<sizeof(FIL)<<", SD FIL="<<sizeof(SD_FATFS_FIL)
             <<", SDK FATFS="<<sizeof(FATFS)<<", SD FATFS="<<sizeof(SD_FATFS_FATFS)<<" (host ABI)\n";
}

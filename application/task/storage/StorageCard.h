#pragma once
#include "../../Config.h"
#include <sd_diskio.h>
namespace geoputer {
#if defined(ARDUINO_ARCH_ESP32) && !defined(GEOPUTER_TEST_FATFS)
uint8_t initStorageCard(uint8_t cs,SPIClass* spi,int hz);
bool mountStorageCard(uint8_t drive,const char* path,uint8_t max_files,bool format_if_empty,int* mount_result=nullptr);
uint8_t unmountStorageCard(uint8_t drive);
uint8_t releaseStorageCard(uint8_t drive);
#else
inline uint8_t initStorageCard(uint8_t cs,SPIClass* spi,int hz) {return ::sdcard_init(cs,spi,hz);}
inline bool mountStorageCard(uint8_t d,const char* path,uint8_t n,bool format,int* result=nullptr) {const bool ok=::sdcard_mount(d,path,n,format);if(result)*result=ok?0:-1;return ok;}
inline uint8_t unmountStorageCard(uint8_t d) {return ::sdcard_unmount(d);}
inline uint8_t releaseStorageCard(uint8_t d) {return ::sdcard_uninit(d);}
#endif
}

#pragma once
#include "SPI.h"
#include "USB.h"
#include "ff.h"
#include <cstring>
inline uint8_t sdcard_init(uint8_t,SPIClass*,int) {if(usb_test::init_error)return 0xff;usb_test::driver=true;return 0;}
inline uint8_t sdcard_uninit(uint8_t) {
    usb_test::driver=usb_test::registered=usb_test::mounted=false;++usb_test::uninitializations;return 0;
}
inline uint32_t sdcard_num_sectors(uint8_t) {return 3;}
inline bool sdcard_mount(uint8_t,const char*,uint8_t,bool format) {
    if(usb_test::registered || !usb_test::driver || HostUSB::mounted || format)return false;
    usb_test::registered=usb_test::mounted=true;++usb_test::mounts;return true;
}
inline uint8_t sdcard_unmount(uint8_t) {usb_test::mounted=false;return 0;} // VFS registration deliberately remains.
inline bool sd_read_raw(uint8_t,uint8_t* out,uint32_t sector) {
    if(usb_test::on_read)usb_test::on_read();
    ++usb_test::reads;
    if(!usb_test::driver || usb_test::mounted || sector>=3)return false;
    std::memcpy(out,usb_test::disk.data()+sector*512,512);return true;
}
inline bool sd_write_raw(uint8_t,uint8_t* in,uint32_t sector) {
    ++usb_test::writes;
    if(!usb_test::driver || usb_test::mounted || sector>=3)return false;
    std::memcpy(usb_test::disk.data()+sector*512,in,512);return true;
}

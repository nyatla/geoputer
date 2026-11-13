#pragma once
#include "ff.h"
#include <cstring>
constexpr int RES_OK=0,CTRL_SYNC=0;
inline int disk_initialize(uint8_t) {return usb_test::driver?0:1;}
inline int disk_ioctl(uint8_t,int,void*) {return usb_test::driver && !usb_test::sync_error?RES_OK:1;}
inline int disk_read(uint8_t,uint8_t* out,uint32_t sector,unsigned count) {
    if(usb_test::on_read)usb_test::on_read();
    ++usb_test::reads;
    if(!usb_test::driver || usb_test::mounted || !count || sector>=3 || count>3-sector)return 1;
    std::memcpy(out,usb_test::disk.data()+sector*512,count*512);return RES_OK;
}
inline int disk_write(uint8_t,const uint8_t* in,uint32_t sector,unsigned count) {
    ++usb_test::writes;
    if(!usb_test::driver || usb_test::mounted || !count || sector>=3 || count>3-sector)return 1;
    std::memcpy(usb_test::disk.data()+sector*512,in,count*512);return RES_OK;
}

#pragma once
#include "ff.h"
constexpr int RES_OK=0,CTRL_SYNC=0;
inline int disk_initialize(uint8_t) {return usb_test::driver?0:1;}
inline int disk_ioctl(uint8_t,int,void*) {return usb_test::driver && !usb_test::sync_error?RES_OK:1;}

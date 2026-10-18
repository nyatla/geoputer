#pragma once
#include "USB.h"
inline void tud_disconnect() {HostUSB::mounted=false;}
inline void tud_connect() {HostUSB::mounted=HostUSB::present.load();}

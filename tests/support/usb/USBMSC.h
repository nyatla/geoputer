#pragma once
#include <cstdint>
class USBMSC {
public:
    inline static USBMSC* instance=nullptr;
    int32_t (*read)(uint32_t,uint32_t,void*,uint32_t)=nullptr;
    int32_t (*write)(uint32_t,uint32_t,uint8_t*,uint32_t)=nullptr;
    bool (*start_stop)(uint8_t,bool,bool)=nullptr;
    bool media=false;
    USBMSC() {instance=this;}
    void vendorID(const char*) {}
    void productID(const char*) {}
    void productRevision(const char*) {}
    void onRead(decltype(read) f) {read=f;}
    void onWrite(decltype(write) f) {write=f;}
    void onStartStop(decltype(start_stop) f) {start_stop=f;}
    bool begin(uint32_t count,uint16_t size) {return count && size==512;}
    void mediaPresent(bool present) {media=present;}
};

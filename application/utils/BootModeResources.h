#pragma once
#include "../BootMode.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
namespace geoputer {
// In-place application-lifetime resources. Select once, before any task starts.
// Reboot is the ownership barrier; running tasks never replace an active member.
struct EmptyWifiResources {};
template<class Gps,class Usb,class Wifi=EmptyWifiResources> class BootModeResources {
public:
    BootModeResources()=default;
    ~BootModeResources(){destroyForShutdown();}
    // Only the outer application owner may call this, never a subtask.
    // All tasks and borrowers must already be quiescent.
    void destroyForShutdown() {
        if(kind_==Kind::GpsActive)data_.gps.~Gps();
        else if(kind_==Kind::UsbActive)data_.usb.~Usb();
        else if(kind_==Kind::WifiActive)data_.wifi.~Wifi();
        kind_=Kind::None;
    }
    BootModeResources(const BootModeResources&)=delete;
    BootModeResources& operator=(const BootModeResources&)=delete;
    struct Tail {uintptr_t begin=0,end=0;size_t bytes()const{return end>begin?end-begin:0;}};
    // Only a boot-lifetime static owner may publish this tail to an allocator.
    // Never includes the live Wi-Fi object, tag or donation latch.
    Tail wifiHeapTail()const {
        if(!isWifi() || tail_donated_)return {};
        const auto base=reinterpret_cast<uintptr_t>(&data_);
        const auto begin=(base+sizeof(Wifi)+15u)&~uintptr_t(15u);
        const auto end=(base+sizeof(data_))&~uintptr_t(15u);
        return begin<end?Tail{begin,end}:Tail{};
    }
    void sealWifiHeapTail(){assert(isWifi() && !tail_donated_);tail_donated_=true;}
    bool selected()const{return kind_!=Kind::None;}
    bool isUsb()const{return kind_==Kind::UsbActive;}
    template<class... Args> bool selectGps(Args&&... args) {
        if(selected())return kind_==Kind::GpsActive;
        if(tail_donated_)return false;
        ::new(static_cast<void*>(&data_.gps)) Gps(std::forward<Args>(args)...);kind_=Kind::GpsActive;return true;
    }
    template<class... Args> bool selectUsb(Args&&... args) {
        if(selected())return kind_==Kind::UsbActive;
        if(tail_donated_)return false;
        ::new(static_cast<void*>(&data_.usb)) Usb(std::forward<Args>(args)...);kind_=Kind::UsbActive;return true;
    }
    bool isWifi()const{return kind_==Kind::WifiActive;}
    template<class... Args> bool selectWifi(Args&&... args) {
        if(selected())return kind_==Kind::WifiActive;
        if(tail_donated_)return false;
        ::new(static_cast<void*>(&data_.wifi)) Wifi(std::forward<Args>(args)...);kind_=Kind::WifiActive;return true;
    }
    Wifi& wifi(){assert(kind_==Kind::WifiActive);return data_.wifi;}
    const Wifi& wifi()const{assert(kind_==Kind::WifiActive);return data_.wifi;}
    Gps& gps(){assert(kind_==Kind::GpsActive);return data_.gps;}
    const Gps& gps()const{assert(kind_==Kind::GpsActive);return data_.gps;}
    Usb& usb(){assert(kind_==Kind::UsbActive);return data_.usb;}
    const Usb& usb()const{assert(kind_==Kind::UsbActive);return data_.usb;}
private:
    enum class Kind:unsigned char {None,GpsActive,UsbActive,WifiActive};
    union Data {Gps gps;Usb usb;Wifi wifi;Data(){}~Data(){}} data_;
    Kind kind_=Kind::None;
    bool tail_donated_=false; // Irreversible until reset; also prevents reuse after shutdown.
};
} // namespace geoputer

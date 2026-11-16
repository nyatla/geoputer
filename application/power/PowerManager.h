#pragma once
#include "../BootMode.h"
#include <cstdint>
namespace geoputer::power {
enum class Level:uint8_t {None=1,Normal=2,Maximum=3};
constexpr bool valid(Level level){return unsigned(level)>=1 && unsigned(level)<=3;}
struct Policy {
    bool audio_available=true;
    bool manage_audio=false;
    bool usb_enabled=true;
};
constexpr Policy policy(Level level,BootMode mode){
    const bool logger=mode==BootMode::GpsLogger;
    // USB storage and Wi-Fi management keep their existing USB transport.
    return {logger,logger && valid(level) && level!=Level::None,
            !logger || level!=Level::Maximum};
}
inline constexpr uint32_t ScreenOffCpuMhz=80;
constexpr bool reduceCpuWhileScreenOff(Level level,BootMode mode){
    return mode==BootMode::GpsLogger && level==Level::Maximum;
}
// Policy only. Hardware is operated by the task which owns that peripheral.
class PowerManager {
public:
    void configure(Level level,BootMode mode){level_=valid(level)?level:Level::None;mode_=mode;}
    Policy current()const{return policy(level_,mode_);}
    Level level()const{return level_;}
    BootMode mode()const{return mode_;}
private:
    Level level_=Level::None;
    BootMode mode_=BootMode::GpsLogger;
};
inline constexpr uint32_t AudioClockWarmMs=40,AudioDacWarmMs=80,AudioDrainMs=20;
// Budget for preparation and a slow UI iteration. The hard 60-second cap is unchanged.
inline constexpr uint32_t AudioPreparationBudgetMs=500;
} // namespace geoputer::power

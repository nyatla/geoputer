#pragma once
#include "../Config.h"
#include <array>
#include <cstdint>
#include <cstring>
namespace geoputer::alarm {
enum class Mode:uint8_t {Every,Relieved,Confirmed};
enum class Tone:uint8_t {Single,Double,Rising,Falling,Chime};
enum class State:uint8_t {Unavailable,Ready,Created,Repaired,IoError,DataError};
inline constexpr uint16_t Widths[]={5,10,20,50,100,200};
// 0 is one pattern; 255 is until silenced, with the same hard 60-second cap.
inline constexpr uint8_t Durations[]={0,3,5,10,30,255};
inline constexpr uint32_t MaximumMs=60000,RearmMs=3000,RepeatGapMs=500;
struct Settings {
    bool enabled=false;
    uint8_t level=1;
    Mode mode=Mode::Relieved;
    uint8_t width=2;
    Tone tone=Tone::Double;
    uint8_t duration=0;
};
inline bool valid(const Settings& s,unsigned depth) {
    return depth && depth<=config::IndexMaxDepth && s.level>=1 && s.level<=depth &&
        unsigned(s.mode)<=2 && s.width<sizeof(Widths)/sizeof(*Widths) && unsigned(s.tone)<=4 && s.duration<sizeof(Durations);
}
inline bool same(const Settings& a,const Settings& b) {
    return a.enabled==b.enabled && a.level==b.level && a.mode==b.mode && a.width==b.width && a.tone==b.tone && a.duration==b.duration;
}
struct Profile {
    Settings settings{};
    uint32_t identity=0,revision=0;
    uint8_t dataset=0,depth=0;
    State state=State::Unavailable;
    bool usable()const {return (state==State::Ready || state==State::Created || state==State::Repaired) && valid(settings,depth);}
};
struct Region {
    std::array<uint32_t,config::IndexMaxDepth> path{};
    uint8_t depth=0;
    bool valid()const {return depth>0 && depth<=path.size();}
};
inline bool same(const Region& a,const Region& b) {
    return a.depth==b.depth && a.valid() && std::memcmp(a.path.data(),b.path.data(),a.depth*sizeof(uint32_t))==0;
}
struct Observation {Region center{};uint8_t agreeing=0,valid_points=0;};
struct AudioCommand {bool play=false;Tone tone=Tone::Single;uint8_t duration=0,volume=1;};
inline const char* stateName(State s) {
    switch(s) {case State::Ready:return "READY";case State::Created:return "CREATED / OFF";
        case State::Repaired:return "CONFIG RESET / OFF";case State::IoError:return "ALERT FILE I/O ERROR";
        case State::DataError:return "ALERT DATA ERROR";default:return "ALERT UNAVAILABLE";}
}
} // namespace geoputer::alarm

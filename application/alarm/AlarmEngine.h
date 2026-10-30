#pragma once
#include "AlarmTypes.h"
namespace geoputer::alarm {
// Keep the last accepted region until the center candidate has enough cell votes.
class Engine {
public:
    void reset() {known_={};}
    bool observe(const Observation& o,const Settings& s) {
        if(!s.enabled || unsigned(s.judgement)>2 || !o.center.valid() ||
           !o.valid_points || o.valid_points>9 || !o.agreeing || o.agreeing>o.valid_points)return false;
        if(!known_.valid()) {known_=o.center;return false;}
        if(same(known_,o.center) || o.agreeing<requiredPoints(s.judgement))return false;
        known_=o.center;return true;
    }
private:
    Region known_{};
};
// A re-alert extends the selected duration, never the first-start hard deadline.
class SoundSession {
public:
    void start(Tone tone,uint8_t duration,uint8_t volume,uint32_t now,uint32_t preparation_ms=0) {
        if(!active(now))started_=now;
        active_=true;tone_=tone;volume_=volume;duration_=duration;
        const auto seconds=Durations[duration<sizeof(Durations)?duration:0];
        until_=now+(seconds==0?cycleMs(tone)+preparation_ms:seconds==255?MaximumMs:uint32_t(seconds)*1000);
    }
    bool active(uint32_t now)const {return active_ && now-started_<MaximumMs && static_cast<int32_t>(now-until_)<0;}
    void stop(){active_=false;}
    AudioCommand command()const {return {true,tone_,duration_,volume_};}
    static uint32_t cycleMs(Tone t) {return t==Tone::Single?180:t==Tone::Chime?720:360;}
    bool oneShot()const{return Durations[duration_]==0;}
private:
    uint32_t started_=0,until_=0;
    bool active_=false;
    Tone tone_=Tone::Single;
    uint8_t volume_=1,duration_=0;
};
} // namespace geoputer::alarm

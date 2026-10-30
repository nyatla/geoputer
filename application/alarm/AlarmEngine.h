#pragma once
#include "AlarmTypes.h"
namespace geoputer::alarm {
// App-owned, fixed state. Suppression survives silence, but not dataset changes.
class Engine {
public:
    void reset() {known_={};left_={};right_={};settling_={};suppressed_=settling_valid_=false;}
    void unavailable(){settling_valid_=false;}
    bool observe(const Observation& o,const Settings& s,uint32_t now) {
        if(!s.enabled || !o.center.valid()){unavailable();return false;}
        const auto& current=o.center;
        if(!known_.valid()) {known_=current;return false;}
        const bool uniform=o.valid_points==9 && o.agreeing==9;
        if(s.mode==Mode::Confirmed) {
            if(!uniform || same(known_,current))return false;
            known_=current;return true;
        }
        if(s.mode==Mode::Every) {
            if(same(known_,current))return false;
            known_=current;return true;
        }
        if(suppressed_) {
            const bool pair=same(current,left_) || same(current,right_);
            if(pair) {
                if(uniform) {
                    if(!settling_valid_ || !same(settling_,current)) {settling_=current;settled_at_=now;settling_valid_=true;}
                    else if(now-settled_at_>=RearmMs) {known_=current;suppressed_=settling_valid_=false;}
                } else settling_valid_=false;
                return false;
            }
            // A third region is a different crossing, not an A/B bounce.
            suppressed_=settling_valid_=false;
        }
        if(same(known_,current))return false;
        left_=known_;right_=current;known_=current;suppressed_=true;settling_valid_=false;
        return true;
    }
private:
    Region known_{},left_{},right_{},settling_{};
    uint32_t settled_at_=0;
    bool suppressed_=false,settling_valid_=false;
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

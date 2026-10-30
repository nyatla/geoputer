#pragma once
#include "PowerManager.h"
namespace geoputer::power {
// UI task only. Nonblocking DAC/output standby; never reset the codec or its CSM.
class AudioPower {
public:
    enum class State:uint8_t {Ready,Standby,ClockWarm,DacWarm,Drain,Fault};
    bool begin(Policy policy,uint32_t now);
    void configure(Policy policy){policy_=policy;}
    bool tick(bool playing,uint32_t now);
    bool ready()const{return state_==State::Ready;}
    bool failed()const{return state_==State::Fault;}
    State state()const{return state_;}
private:
    bool capture();
    bool codec(bool on,bool muted);
    bool mute(bool on);
    bool fail();
    bool startClocks(uint32_t now);
    bool expired(uint32_t now)const{return static_cast<int32_t>(now-next_at_)>=0;}
    Policy policy_{};
    State state_=State::Ready;
    uint32_t next_at_=0;
    uint8_t base_mute_=0,base_dac_=0,base_output_=0x10;
    bool adv_=false,captured_=false,drain_clock_=false;
};
} // namespace geoputer::power

#include "AudioPower.h"
#include "../utils/Console.h"
#include <M5Cardputer.h>
namespace geoputer::power {
namespace {
// ROM silence, infinitely repeated on the same channel subsequently used by tone().
constexpr int16_t Silence[64]={};
bool read(uint8_t reg,uint8_t& value){return M5.In_I2C.readRegister(0x18,reg,&value,1,400000);}
bool write(uint8_t reg,uint8_t value){return M5.In_I2C.writeRegister8(0x18,reg,value,400000);}
}
bool AudioPower::capture(){
    if(captured_ || !adv_)return true;
    if(!read(0x31,base_mute_) || !read(0x12,base_dac_) || !read(0x13,base_output_))return false;
    base_mute_&=0x9f;base_dac_&=~0x02;base_output_|=0x10;captured_=true;
    return true;
}
bool AudioPower::mute(bool enabled){return !adv_ || write(0x31,base_mute_|(enabled?0x60:0));}
bool AudioPower::codec(bool on,bool muted){
    if(!adv_)return true;
    // ES8311 DAC mute follows Espressif's es8311_set_mute(). DAC standby uses
    // SYSTEM12 bit1 as in M5Unified. Keep RESET00, SYSTEM0D/0E and clocks intact.
    bool ok=mute(true);
    if(on){ok=write(0x12,base_dac_)&&ok;ok=write(0x13,base_output_)&&ok;}
    else {ok=write(0x13,base_output_&~0x10)&&ok;ok=write(0x12,base_dac_|0x02)&&ok;}
    if(on && !muted)ok=mute(false)&&ok;
    uint8_t dac=0,output=0;
    return ok && read(0x12,dac) && read(0x13,output) &&
        bool(dac&0x02)==!on && bool(output&0x10)==on;
}
bool AudioPower::fail(){
    M5.Speaker.stop();state_=State::Fault;
    // Best effort mute. Do not restart or repeatedly hit a failed I2C bus.
    if(adv_ && captured_)mute(true);
    Console.println("[POWER] audio=ERROR");return false;
}
bool AudioPower::begin(Policy policy,uint32_t now){
    policy_=policy;adv_=M5.getBoard()==m5::board_t::board_M5CardputerADV;
    if(!policy_.audio_available){
        if(!capture() || !codec(false,true))return fail();
        state_=State::Standby;Console.println("[POWER] audio=UNUSED STANDBY");return true;
    }
    state_=State::Ready;return tick(false,now);
}
bool AudioPower::startClocks(uint32_t now){
    if(!capture() || !mute(true) || !M5.Speaker.playRaw(Silence,64,16000,false,UINT32_MAX,0,true))return fail();
    state_=State::ClockWarm;next_at_=now+AudioClockWarmMs;
    Console.println("[POWER] audio=CLOCK WARM");return true;
}
bool AudioPower::tick(bool playing,uint32_t now){
    if(state_==State::Fault)return false;
    if(!policy_.audio_available)return true;
    const bool wanted=playing || !policy_.manage_audio;
    if(wanted && (state_==State::Standby || state_==State::Drain))return startClocks(now);
    if(!wanted && (state_==State::Ready || state_==State::ClockWarm || state_==State::DacWarm)){
        M5.Speaker.stop();state_=State::Drain;drain_clock_=false;return true;
    }
    if(state_==State::ClockWarm && expired(now)){
        if(!codec(true,true))return fail();
        state_=State::DacWarm;next_at_=now+AudioDacWarmMs;
    }else if(state_==State::DacWarm && expired(now)){
        if(!mute(false))return fail();
        state_=State::Ready;if(!playing)M5.Speaker.stop();
        Console.println("[POWER] audio=READY");
    }else if(state_==State::Drain){
        if(M5.Speaker.isPlaying(0)){drain_clock_=false;return true;}
        // isPlaying() describes source buffers; allow the DMA tail to drain too.
        if(!drain_clock_){next_at_=now+AudioDrainMs;drain_clock_=true;}
        else if(expired(now)){
            if(!capture() || !codec(false,true))return fail();
            state_=State::Standby;Console.println("[POWER] audio=STANDBY (DAC/output)");
        }
    }
    return true;
}
} // namespace geoputer::power

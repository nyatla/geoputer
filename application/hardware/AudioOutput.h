#pragma once
#include "../alarm/AlarmEngine.h"
#include "../power/AudioPower.h"
namespace geoputer {
// UI task only. Driver, DMA and ROM silence are retained; no per-alert heap.
class AudioOutput {
public:
    bool begin(power::Policy policy={},uint32_t now=0);
    void setPowerPolicy(power::Policy policy){policy_=policy;power_.configure(policy);}
    bool command(const alarm::AudioCommand& command,uint32_t now);
    void tick(uint32_t now);
    bool active()const{return active_;}
private:
    void stop(uint32_t now);
    power::Policy policy_{};
    power::AudioPower power_;
    alarm::SoundSession session_;
    uint32_t next_at_=0;
    uint8_t step_=0;
    alarm::Tone tone_=alarm::Tone::Single;
    bool ready_=false,active_=false,pattern_started_=false;
};
} // namespace geoputer

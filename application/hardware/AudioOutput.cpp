#include "AudioOutput.h"
#include <M5Cardputer.h>
#include "../utils/Console.h"
namespace geoputer {
namespace {
struct Note{uint16_t hz,ms;};
constexpr Note Patterns[5][3]={{{880,160},{0,0},{0,0}},{{880,120},{0,60},{880,120}},{{660,160},{1046,160},{0,0}},{{1046,160},{660,160},{0,0}},{{784,200},{988,200},{1175,240}}};
constexpr uint8_t Counts[]={1,3,2,2,3},Volumes[]={24,48,72,104,144};
}
bool AudioOutput::begin(power::Policy policy,uint32_t now) {
    policy_=policy;
    if(!policy.audio_available){power_.begin(policy,now);return false;}
    auto cfg=M5.Speaker.config();cfg.sample_rate=16000;cfg.dma_buf_len=64;cfg.dma_buf_count=4;
    M5.Speaker.config(cfg);ready_=M5.Speaker.begin();
    ready_=ready_ && power_.begin(policy,now);
    Console.printf("[AUDIO] ready=%u managed=%u\n",ready_,policy.manage_audio);
    return ready_;
}
void AudioOutput::stop(uint32_t now) {
    M5.Speaker.stop();session_.stop();active_=false;step_=0;pattern_started_=false;
    power_.tick(false,now);
    Console.printf("[AUDIO] idle managed=%u\n",policy_.manage_audio);
}
bool AudioOutput::command(const alarm::AudioCommand& c,uint32_t now) {
    if(!c.play){if(active_)stop(now);return true;}
    if(!ready_ || !policy_.audio_available || power_.failed() || unsigned(c.tone)>=5 || c.duration>=sizeof(alarm::Durations) || c.volume>=5)return false;
    if(!session_.active(now))session_.stop();
    session_.start(c.tone,c.duration,c.volume,now,policy_.manage_audio?power::AudioPreparationBudgetMs:0);
    tone_=c.tone;step_=0;next_at_=now;pattern_started_=false;
    M5.Speaker.stop();M5.Speaker.setVolume(Volumes[c.volume]);active_=true;
    if(!power_.tick(true,now)){stop(now);return false;}
    Console.printf("[AUDIO] play tone=%u volume=%u managed=%u\n",unsigned(c.tone),c.volume+1,policy_.manage_audio);
    return true;
}
void AudioOutput::tick(uint32_t now) {
    if(!power_.tick(active_,now)){if(active_)stop(now);return;}
    if(!active_)return;
    if(!session_.active(now)){stop(now);return;}
    if(!power_.ready())return;
    if(!pattern_started_){next_at_=now;pattern_started_=true;}
    if(static_cast<int32_t>(now-next_at_)<0)return;
    const auto t=unsigned(tone_);
    if(step_>=Counts[t]) {
        if(session_.oneShot()){if(!M5.Speaker.isPlaying(0))stop(now);return;}
        step_=0;next_at_=now+alarm::RepeatGapMs;return;
    }
    const auto note=Patterns[t][step_++];next_at_=now+note.ms;
    if(note.hz && !M5.Speaker.tone(note.hz,note.ms,0,true))stop(now);
}
} // namespace geoputer

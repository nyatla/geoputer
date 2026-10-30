#include "hardware/AudioOutput.h"
#include "utils/Console.h"
#include <M5Cardputer.h>
#include <cstdio>
#include <stdexcept>
namespace geoputer {USBCDC Console;}
using namespace geoputer;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void advance(AudioOutput& audio,uint32_t now){M5.advance(now);audio.tick(now);}
void run(AudioOutput& audio,uint32_t first,unsigned ms){for(unsigned n=0;n<=ms;n+=10)advance(audio,first+n);}
void policyTests(){
    for(auto level:{power::Level::None,power::Level::Normal,power::Level::Maximum}){
        const auto gps=power::policy(level,BootMode::GpsLogger),usb=power::policy(level,BootMode::UsbStorage);
        check(gps.audio_available && gps.manage_audio==(level!=power::Level::None),"GPS policy level");
        check(!usb.audio_available && usb.usb_enabled && gps.usb_enabled,"USB unchanged; unused audio disabled");
    }
    power::PowerManager manager;manager.configure(static_cast<power::Level>(0),BootMode::GpsLogger);
    check(manager.level()==power::Level::None,"invalid level falls back safely");
}
int main(){try{
    policyTests();
    AudioOutput audio;check(audio.begin(),"init");
    check(M5.Speaker.begins==1 && M5.Speaker.cfg.dma_buf_len==64 && M5.Speaker.cfg.dma_buf_count==4,"one boot driver allocation");
    const unsigned notes[]={1,2,2,2,3};uint32_t now=1000;
    for(unsigned tone=0;tone<5;++tone){
        M5.advance(now);const auto before=M5.Speaker.tones;
        check(audio.command({true,static_cast<alarm::Tone>(tone),0,static_cast<uint8_t>(tone)},now),"unmanaged tone accepted");
        run(audio,now,900);check(!audio.active() && M5.Speaker.tones-before==notes[tone],"unmanaged ROM pattern once");now+=2000;
    }
    check(M5.In_I2C.writes==0,"None policy has no codec power writes");
    for(auto level:{power::Level::Normal,power::Level::Maximum}){
        audio.setPowerPolicy(power::policy(level,BootMode::GpsLogger));run(audio,now,100);now+=200;
        check((M5.In_I2C.regs[0x12]&2) && !(M5.In_I2C.regs[0x13]&0x10),"idle DAC/output standby");
        for(unsigned repeat=0;repeat<20;++repeat){
            const unsigned tone=repeat%5;M5.advance(now);const auto before=M5.Speaker.tones;
            check(audio.command({true,static_cast<alarm::Tone>(tone),0,1},now),"managed audition accepted after idle");
            check(M5.Speaker.tones==before,"tone waits for clock/DAC readiness");
            run(audio,now,1300);check(!audio.active() && M5.Speaker.tones-before==notes[tone],"managed pattern completes once");
            check((M5.In_I2C.regs[0x12]&2) && !(M5.In_I2C.regs[0x13]&0x10),"managed audition returns to standby");
            now+=3000;
        }
    }
    check(!M5.In_I2C.deep_writes && !M5.early_dac && !M5.early_shutdown && !M5.bad_tones,"clock first; DMA drain before standby; no deep reset; audible tones only when ready");
    check(M5.Speaker.begins==1,"all level changes and auditions retain driver allocation");
    audio.setPowerPolicy(power::policy(power::Level::None,BootMode::GpsLogger));run(audio,now,200);now+=300;
    check(!(M5.In_I2C.regs[0x12]&2) && !M5.Speaker.playing,"None restores codec but parks silence generator");
    audio.setPowerPolicy(power::policy(power::Level::Normal,BootMode::GpsLogger));run(audio,now,100);now+=200;
    M5.advance(now);check(audio.command({true,alarm::Tone::Double,5,0},now),"continuous accepted");
    run(audio,now,200);now+=200;check(audio.command({},now) && !audio.active(),"mute stops during session");
    run(audio,now,100);now+=200;check(M5.In_I2C.regs[0x12]&2,"mute drains and powers down");
    M5.advance(now);check(audio.command({true,alarm::Tone::Single,5,0},now),"hard cap start");
    run(audio,now,200);M5.advance(now+59999);check(audio.command({true,alarm::Tone::Double,5,1},now+59999),"re-alert accepted");
    advance(audio,now+60000);check(!audio.active(),"re-alert cannot extend first 60 seconds");now+=60200;
    run(audio,now,100);now+=200;
    check(!audio.command({true,static_cast<alarm::Tone>(5),0,0},now),"invalid tone");
    check(!audio.command({true,alarm::Tone::Single,6,0},now),"invalid duration");
    check(!audio.command({true,alarm::Tone::Single,0,5},now),"invalid volume");
    M5.In_I2C.ok=false;M5.advance(now);check(!audio.command({true,alarm::Tone::Single,0,0},now) && !audio.active(),"power I2C failure rejects sound safely");
    check(M5.Speaker.begins==1,"failure does not recreate driver");
    M5={};AudioOutput unused;check(!unused.begin(power::policy(power::Level::Normal,BootMode::UsbStorage),0),"unused USB audio unavailable");
    check(M5.Speaker.begins==0 && (M5.In_I2C.regs[0x12]&2),"USB audio fixed standby without creating audio worker");
    std::puts("PASS: all power levels, forty managed auditions, clock-first wake/DAC settle, DMA drain, None restore, USB audio exclusion, mute, hard cap and I2C failure; driver retained");
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}

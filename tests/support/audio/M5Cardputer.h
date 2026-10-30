#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace m5 {enum class board_t{board_M5CardputerADV};}
inline uint32_t mock_now=0;
struct MockSpeaker {
    struct Config {unsigned sample_rate=0,dma_buf_len=0,dma_buf_count=0;} cfg;
    unsigned begins=0,stops=0,tones=0,primers=0,volume=0,last_hz=0;
    uint32_t done_at=0,clock_at=0,stopped_at=0;
    bool begin_ok=true,tone_ok=true,playing=false,clock=false,infinite=false;
    Config config()const{return cfg;} void config(Config c){cfg=c;}
    bool begin(){++begins;return begin_ok;}
    void stop(){++stops;infinite=false;if(playing)done_at=mock_now+10;stopped_at=mock_now;}
    void setVolume(unsigned v){volume=v;}
    unsigned isPlaying(unsigned)const{return playing;}
    bool tone(unsigned hz,unsigned ms,unsigned,bool);
    bool playRaw(const int16_t*,size_t,unsigned,bool,uint32_t,unsigned,bool){
        ++primers;playing=infinite=true;clock_at=mock_now+30;return true;
    }
};
struct MockI2c {
    std::array<uint8_t,256> regs=[](){std::array<uint8_t,256> r{};r[0x13]=0x10;return r;}();
    unsigned writes=0,deep_writes=0;bool ok=true;
    bool readRegister(unsigned,uint8_t reg,uint8_t* out,size_t,unsigned){if(ok)*out=regs[reg];return ok;}
    bool writeRegister8(unsigned,uint8_t reg,uint8_t value,unsigned);
};
struct MockM5 {
    MockSpeaker Speaker;MockI2c In_I2C;unsigned early_dac=0,early_shutdown=0,bad_tones=0;
    m5::board_t getBoard()const{return m5::board_t::board_M5CardputerADV;}
    void advance(uint32_t now){mock_now=now;if(Speaker.playing){
        if(Speaker.infinite){if(static_cast<int32_t>(now-Speaker.clock_at)>=0)Speaker.clock=true;}
        else if(static_cast<int32_t>(now-Speaker.done_at)>=0){Speaker.playing=false;Speaker.clock=false;}
    }}
};
inline MockM5 M5;
inline bool MockSpeaker::tone(unsigned hz,unsigned ms,unsigned,bool){
    ++tones;last_hz=hz;
    if((M5.In_I2C.regs[0x12]&2) || !(M5.In_I2C.regs[0x13]&0x10) || (M5.In_I2C.regs[0x31]&0x60))++M5.bad_tones;
    playing=true;infinite=false;done_at=mock_now+ms;clock=true;return tone_ok;
}
inline bool MockI2c::writeRegister8(unsigned,uint8_t reg,uint8_t value,unsigned){
    ++writes;if(reg==0 || reg==0x0D || reg==0x0E)++deep_writes;
    if(ok){
        if(reg==0x12 && !(value&2) && !M5.Speaker.clock)++M5.early_dac;
        if(reg==0x12 && (value&2) && (M5.Speaker.playing || mock_now-M5.Speaker.stopped_at<20))++M5.early_shutdown;
        regs[reg]=value;
    }
    return ok;
}

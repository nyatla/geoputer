#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <utility/Adafruit_TCA8418/Adafruit_TCA8418_registers.h>
// Electrical model: a LOW output column pulls down only its pressed row inputs.
class Adafruit_TCA8418 {
public:
    inline static std::array<uint8_t,256> regs{};
    inline static std::deque<uint8_t> fifo;
    inline static uint64_t physical = 0;
    inline static bool online=true, latched=false;
    inline static unsigned gpio_reads=0, fail_gpio_read=0, reads=0;
    static void reset() { regs={};fifo.clear();physical=0;online=true;latched=false;gpio_reads=fail_gpio_read=reads=0; }
    bool begin() { return online; }
    bool writeRegister8(uint8_t reg,uint8_t value) {
        if(!online)return false;
        if(reg==TCA8418_REG_GPIO_DIR_2 && value) {
            if((value & (value-1)) || regs[TCA8418_REG_GPIO_DAT_OUT_2])throw std::runtime_error("opposed matrix outputs");
            latched=true;
        }
        if(reg==TCA8418_REG_INT_STAT)regs[reg]&=~value;
        else regs[reg]=value;
        return true;
    }
    bool readRegister(uint8_t reg,uint8_t* out,unsigned size) {
        ++reads;if(!online || size!=1)return false;
        if(reg==TCA8418_REG_GPIO_DAT_STAT_1) {
            if(++gpio_reads==fail_gpio_read)return false;
            if(latched){latched=false;*out=0;return true;} // Previously latched value, cleared by reading.
            uint8_t rows=0xff;
            if(!regs[TCA8418_REG_KP_GPIO_1] && !regs[TCA8418_REG_KP_GPIO_2])
                for(unsigned col=0;col<8;++col)if(regs[TCA8418_REG_GPIO_DIR_2]&(1u<<col))
                    rows&=~static_cast<uint8_t>(physical>>(col*7));
            *out=rows;return true;
        }
        if(reg==TCA8418_REG_KEY_EVENT_A) {
            *out=fifo.empty()?0:fifo.front();if(!fifo.empty())fifo.pop_front();return true;
        }
        if(reg==TCA8418_REG_KEY_LCK_EC){*out=static_cast<uint8_t>(fifo.size());return true;}
        *out=regs[reg];return true;
    }
};

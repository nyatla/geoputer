#pragma once
#include <cstdint>
inline unsigned keyboard_settles = 0;
inline void delayMicroseconds(unsigned value) { if (value >= 200) ++keyboard_settles; }
#define IRAM_ATTR
inline constexpr int OUTPUT=1, INPUT_PULLUP=2, INPUT=0, CHANGE=3;
using gpio_num_t=int;
inline uint8_t keyboard_scan_column=0;
inline uint64_t keyboard_matrix_held=0;
inline void gpio_reset_pin(gpio_num_t) {}
inline void pinMode(int,int) {}
inline void digitalWrite(int pin,int value) {
    const unsigned bit=pin==8?0:pin==9?1:2;
    keyboard_scan_column=(keyboard_scan_column&~(1u<<bit))|((value!=0)<<bit);
}
inline int digitalRead(int pin) {
    constexpr int pins[]={13,15,3,4,5,6,7};
    for(unsigned row=0;row<7;++row)if(pins[row]==pin)
        return (keyboard_matrix_held>>(keyboard_scan_column*7+row))&1?0:1;
    return 1;
}
inline int digitalPinToInterrupt(int pin) {return pin;}
inline void (*keyboard_irq)(void*)=nullptr;
inline void* keyboard_irq_arg=nullptr;
inline void attachInterruptArg(int,void (*handler)(void*),void* arg,int) {keyboard_irq=handler;keyboard_irq_arg=arg;}

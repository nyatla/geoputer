#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>
#include "Config.h"
#include "hardware/LoraSafety.h"
#include <array>
#include <cassert>
#include <iostream>
#include <vector>
#include <limits>

namespace {
struct Pin { uint8_t latch = HIGH, level = HIGH, mode = 0; bool held = false, touched = false; };
std::array<Pin,49> pins;
uint32_t now = 0, released_at = 0, asserted_at = 0;
bool absent = false, stuck_after_status = false, accept_sleep = true, sleeping = false;
bool fail_nss_hold = false, transaction = false;
uint8_t status = 0x22;
std::vector<std::vector<uint8_t>> frames;
void allowed(uint8_t pin) { assert(pin == 3 || pin == 5 || pin == 6); }
void output(uint8_t pin) {
    auto& p=pins[pin]; if(p.held || p.mode!=OUTPUT) return;
    if(pin==3 && p.level!=p.latch) {
        if(p.latch==LOW) { asserted_at=now; sleeping=false; }
        else { assert(static_cast<uint32_t>(now-asserted_at)>=1); released_at=now; }
    }
    p.level=p.latch;
}
void reset(uint32_t clock=0) {
    pins={}; now=clock; asserted_at=clock; absent=stuck_after_status=sleeping=false;
    accept_sleep=true; fail_nss_hold=transaction=false; status=0x22; frames.clear();
}
void safe() { assert(pins[3].level==LOW && pins[3].held && pins[5].level==HIGH); }
void asleep() { assert(sleeping && pins[3].level==HIGH && pins[3].held && pins[5].level==HIGH && pins[5].held); }
}
void digitalWrite(uint8_t pin,uint8_t value) {
    allowed(pin); assert(value==LOW || value==HIGH);
    pins[pin].latch=value; pins[pin].touched=true; output(pin);
}
void pinMode(uint8_t pin,uint8_t mode) {
    allowed(pin); assert((pin==6 && mode==INPUT_PULLUP) || (pin!=6 && mode==OUTPUT));
    pins[pin].mode=mode; pins[pin].touched=true; output(pin);
}
int gpio_hold_en(gpio_num_t pin) {
    assert(pin==3 || pin==5); assert(pins[pin].mode==OUTPUT);
    if(pin==5 && fail_nss_hold) return -1;
    pins[pin].held=true; return ESP_OK;
}
int gpio_hold_dis(gpio_num_t pin) {
    assert(pin==3 || pin==5); pins[pin].held=false; output(pin); return ESP_OK;
}
uint32_t millis() { return now; }
void delay(uint32_t ms) { now+=ms; }
int digitalRead(uint8_t pin) {
    assert(pin==6);
    return absent || pins[3].level==LOW || sleeping || (stuck_after_status && !frames.empty()) ||
        static_cast<uint32_t>(now-released_at)<2 ? HIGH : LOW;
}
void SPIClass::beginTransaction(const SPISettings& settings) {
    assert(!transaction && !sleeping && pins[3].level==HIGH && pins[5].level==HIGH);
    assert(settings.hz==1000000 && settings.order==MSBFIRST && settings.mode==SPI_MODE0);
    transaction=true; frames.emplace_back();
}
uint8_t SPIClass::transfer(uint8_t value) {
    assert(transaction && !sleeping && pins[3].level==HIGH && pins[5].level==LOW);
    auto& frame=frames.back(); frame.push_back(value); assert(frame.size()<=2);
    if(frame.size()==1) assert(value==0xc0 || value==0x84); // No TX, RX or RF setup command.
    else assert(value==0);
    return frame.size()==2 && frame[0]==0xc0 ? status : 0;
}
void SPIClass::endTransaction() {
    assert(transaction && pins[5].level==HIGH && frames.back().size()==2);
    transaction=false;
    if(frames.back()[0]==0x84 && accept_sleep) sleeping=true;
}
int main() {
    static_assert(geoputer::config::LoraResetPin==3 && geoputer::config::LoraSelectPin==5 && geoputer::config::LoraBusyPin==6);
    SPIClass spi;
    reset(); geoputer::holdUnusedLoraInReset(); safe();
    geoputer::holdUnusedLoraInReset(); safe();
    assert(geoputer::sleepUnusedLora(spi)); asleep();
    assert((frames==std::vector<std::vector<uint8_t>>{{0xc0,0},{0x84,0}}));
    // Shared SD traffic or GPIO probing cannot select/wake the held radio.
    digitalWrite(5,LOW); assert(pins[5].level==HIGH); digitalWrite(5,HIGH);
    digitalWrite(3,LOW); assert(pins[3].level==HIGH);
    // Warm restart must safely replace the previous HIGH reset-pad hold.
    geoputer::holdUnusedLoraInReset(); safe(); frames.clear();
    assert(geoputer::sleepUnusedLora(spi)); asleep();
    // Reserved bits and historical command results must not reject current STDBY_RC.
    // Hardware returns 0x2a after reset; this is a valid prerequisite for SetSleep.
    for(const auto good: {0x20,0x22,0xa2,0x23,0xa3,0x26,0x28,0x2a}) {
        reset(); geoputer::holdUnusedLoraInReset(); status=good;
        uint8_t observed=0;
        assert(geoputer::sleepUnusedLora(spi,&observed)); asleep(); assert(observed==good);
    }
    for(const auto bad: {0x00,0xff,0x30,0x40,0x50,0x60}) {
        reset(); geoputer::holdUnusedLoraInReset(); status=bad;
        assert(!geoputer::sleepUnusedLora(spi)); safe(); assert(frames.size()==1);
    }
    reset(); geoputer::holdUnusedLoraInReset(); absent=true;
    const auto start=now;
    assert(!geoputer::sleepUnusedLora(spi)); safe(); assert(frames.empty()); assert(now-start<=103);
    reset(); geoputer::holdUnusedLoraInReset(); stuck_after_status=true;
    assert(!geoputer::sleepUnusedLora(spi)); safe(); assert(frames.size()==1);
    reset(); geoputer::holdUnusedLoraInReset(); accept_sleep=false;
    assert(!geoputer::sleepUnusedLora(spi)); safe(); assert(frames.size()==2);
    reset(); geoputer::holdUnusedLoraInReset(); fail_nss_hold=true;
    assert(!geoputer::sleepUnusedLora(spi)); safe();
    reset(std::numeric_limits<uint32_t>::max()-50); geoputer::holdUnusedLoraInReset(); absent=true;
    const auto wrap_start=now;
    assert(!geoputer::sleepUnusedLora(spi)); safe(); assert(static_cast<uint32_t>(now-wrap_start)<=103);
    for(unsigned pin=0;pin<pins.size();++pin) if(pin!=3 && pin!=5 && pin!=6) assert(!pins[pin].touched);
    std::cout << "LoRa cold-SLEEP and reset fallback safety tests passed\n";
}

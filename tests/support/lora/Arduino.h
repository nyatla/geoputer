#pragma once
#include <cstdint>
constexpr uint8_t LOW = 0, HIGH = 1, OUTPUT = 3, INPUT_PULLUP = 5;
void digitalWrite(uint8_t pin, uint8_t value);
void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);
uint32_t millis();
void delay(uint32_t ms);

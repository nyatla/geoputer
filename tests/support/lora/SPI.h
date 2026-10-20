#pragma once
#include <cstdint>
constexpr uint8_t MSBFIRST = 1, SPI_MODE0 = 0;
struct SPISettings {
    uint32_t hz; uint8_t order, mode;
    SPISettings(uint32_t h, uint8_t o, uint8_t m): hz(h), order(o), mode(m) {}
};
class SPIClass {
public:
    void beginTransaction(const SPISettings& settings);
    uint8_t transfer(uint8_t value);
    void endTransaction();
};

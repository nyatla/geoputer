#pragma once

#include <utility/Keyboard/KeyboardReader/KeyboardReader.h>
#include <utility/Adafruit_TCA8418/Adafruit_TCA8418.h>
#include <cstdint>

namespace geoputer {
// UI-owned adapter. The ADV keypad controller survives an ESP32 reset.
class AdvKeyboardReader final : public KeyboardReader {
public:
    explicit AdvKeyboardReader(std::size_t max_keys = PhysicalKeyCount) : KeyboardReader(max_keys) {}
    void begin() override;
    void update() override;
    bool ready() const { return ready_; }
private:
    bool snapshot();
    void setMatrixKey(unsigned row, unsigned column, bool pressed);
    bool read(uint8_t reg, uint8_t& value) { return chip_.readRegister(reg, &value, 1); }
    Adafruit_TCA8418 chip_;
    bool ready_ = false;
};
}

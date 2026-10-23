#include "AdvKeyboardReader.h"
#include <Arduino.h>
#include <algorithm>

namespace geoputer {
void AdvKeyboardReader::setKey(unsigned row, unsigned column, bool pressed) {
    const Point2D_t point{static_cast<int>(row * 2 + (column > 3)), static_cast<int>((column + 4) % 4)};
    const auto found = std::find(_key_list.begin(), _key_list.end(), point);
    if (pressed && found == _key_list.end()) _key_list.push_back(point);
    else if (!pressed && found != _key_list.end()) _key_list.erase(found);
}

bool AdvKeyboardReader::snapshot() {
    // GPIO mode supplies the physical state even when no press event remains.
    // Drive just one column LOW; all others are inputs, never opposing outputs.
    bool ok = chip_.writeRegister8(TCA8418_REG_CFG, 0);
    for (const auto reg : {TCA8418_REG_GPIO_DIR_1,TCA8418_REG_GPIO_DIR_2,TCA8418_REG_GPIO_DIR_3,
                          TCA8418_REG_KP_GPIO_1,TCA8418_REG_KP_GPIO_2,TCA8418_REG_KP_GPIO_3,
                          TCA8418_REG_GPI_EM_1,TCA8418_REG_GPI_EM_2,TCA8418_REG_GPI_EM_3,
                          TCA8418_REG_GPIO_PULL_1,TCA8418_REG_GPIO_PULL_2,TCA8418_REG_GPIO_PULL_3,
                          TCA8418_REG_GPIO_DAT_OUT_2,TCA8418_REG_KEY_LCK_EC})
        ok &= chip_.writeRegister8(reg, 0);
    uint64_t held = 0;
    for (unsigned column = 0; column < 8; ++column) {
        ok &= chip_.writeRegister8(TCA8418_REG_GPIO_DIR_2, static_cast<uint8_t>(1u << column));
        delayMicroseconds(200); // Exceeds the documented maximum GPIO debounce time (120us).
        uint8_t rows = 0xff;
        ok &= read(TCA8418_REG_GPIO_DAT_STAT_1, rows); // Clear a previously latched status.
        ok &= read(TCA8418_REG_GPIO_DAT_STAT_1, rows); // Current levels.
        held |= static_cast<uint64_t>((~rows) & 0x7f) << (column * 7);
    }
    // Clear stale events while scanning is disabled, before returning to keypad
    // mode. A bounded drain also handles a disconnected device returning 0xff.
    for (unsigned n = 0; n < 10; ++n) {
        uint8_t event = 0;
        ok &= read(TCA8418_REG_KEY_EVENT_A, event);
        if (!event) break;
    }
    ok &= chip_.writeRegister8(TCA8418_REG_INT_STAT, 0x1f);
    // Restore peripheral ownership on every path, including failed I2C reads.
    ok &= chip_.writeRegister8(TCA8418_REG_GPIO_DIR_2, 0);
    ok &= chip_.writeRegister8(TCA8418_REG_KP_GPIO_1, 0x7f);
    ok &= chip_.writeRegister8(TCA8418_REG_KP_GPIO_2, 0xff);
    ok &= chip_.writeRegister8(TCA8418_REG_KP_GPIO_3, 0);
    if (!ok) return false;
    _key_list.clear();
    for (unsigned column = 0; column < 8; ++column)
        for (unsigned row = 0; row < 7; ++row)
            if (held & (uint64_t{1} << (column * 7 + row))) setKey(row, column, true);
    return true;
}

void AdvKeyboardReader::begin() {
    _key_list.clear();
    ready_ = chip_.begin();
    if (ready_) ready_ = snapshot();
}

void AdvKeyboardReader::update() {
    if (!ready_) return;
    uint8_t status = 0, count = 0;
    if (!read(TCA8418_REG_INT_STAT, status)) return;
    if (status & TCA8418_REG_STAT_OVR_FLOW_INT) { snapshot(); return; }
    // Poll the FIFO count; an already-low IRQ at reset needs no new edge.
    if (!read(TCA8418_REG_KEY_LCK_EC, count)) return;
    count &= 0x0f;
    if (count > 10) return;
    for (unsigned n = 0; n < count; ++n) {
        uint8_t event = 0;
        if (!read(TCA8418_REG_KEY_EVENT_A, event)) return;
        const unsigned key = event & 0x7f;
        if (!key || key > 68) continue;
        const unsigned row = (key - 1) / 10, column = (key - 1) % 10;
        if (row < 7 && column < 8) setKey(row, column, (event & 0x80) != 0);
    }
    chip_.writeRegister8(TCA8418_REG_INT_STAT, 0x1f);
}
}

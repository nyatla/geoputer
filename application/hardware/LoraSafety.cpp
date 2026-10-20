#include "LoraSafety.h"
#include "../Config.h"
#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>

namespace geoputer {

void holdUnusedLoraInReset() {
    // Program the latch BEFORE releasing a pad held HIGH by the previous boot.
    digitalWrite(config::LoraResetPin, LOW);
    pinMode(config::LoraResetPin, OUTPUT);
    gpio_hold_dis(static_cast<gpio_num_t>(config::LoraResetPin));
    gpio_hold_en(static_cast<gpio_num_t>(config::LoraResetPin));
    digitalWrite(config::LoraSelectPin, HIGH);
    pinMode(config::LoraSelectPin, OUTPUT);
}

bool sleepUnusedLora(SPIClass& spi, unsigned char* observed_status) {
    if (observed_status) *observed_status = 0xff;
    const auto fail = [] {
        holdUnusedLoraInReset();
        gpio_hold_en(static_cast<gpio_num_t>(config::LoraSelectPin));
        return false;
    };
    // NRST is still held LOW: it is safe to release last boot's NSS hold.
    if (gpio_hold_dis(static_cast<gpio_num_t>(config::LoraSelectPin)) != ESP_OK) return fail();
    pinMode(config::LoraBusyPin, INPUT_PULLUP); // A missing module times out safely.
    delay(1); // NRESET minimum assertion is 100 us; keep it LOW for at least 1 ms.
    if (gpio_hold_dis(static_cast<gpio_num_t>(config::LoraResetPin)) != ESP_OK) return fail();
    digitalWrite(config::LoraResetPin, HIGH); // Reset defaults to STDBY_RC, never TX/RX.
    delay(1);
    const auto ready = [] {
        const uint32_t started = millis();
        while (digitalRead(config::LoraBusyPin) != LOW) {
            if (static_cast<uint32_t>(millis() - started) >= config::LoraBusyTimeoutMs) return false;
            delay(1);
        }
        return true;
    };
    const auto command = [&](uint8_t opcode, uint8_t parameter) {
        spi.beginTransaction(SPISettings(config::LoraSpiHz, MSBFIRST, SPI_MODE0));
        digitalWrite(config::LoraSelectPin, LOW);
        spi.transfer(opcode);
        const uint8_t reply = spi.transfer(parameter);
        digitalWrite(config::LoraSelectPin, HIGH);
        spi.endTransaction();
        return reply;
    };
    if (!ready()) return fail();
    const uint8_t status = command(0xc0, 0x00); // GetStatus; a floating MISO is not accepted.
    if (observed_status) *observed_status = status;
    // GetStatus also reports the PREVIOUS operation's result (e.g. 0x2a after
    // reset). SetSleep requires current STDBY, not success of that past command.
    // Reserved bits 7/0 are ignored. BUSY and the post-SLEEP transition are checked.
    if ((status & 0x70) != 0x20 || !ready()) return fail();
    command(0x84, 0x00); // Cold SLEEP: no retained configuration or RTC wakeup.
    delay(1); // No further radio SPI access during or after the 500 us transition.
    if (digitalRead(config::LoraBusyPin) != HIGH) return fail();
    // NSS falling wakes SLEEP. Lock both control pads against accidental changes.
    if (gpio_hold_en(static_cast<gpio_num_t>(config::LoraSelectPin)) != ESP_OK ||
        gpio_hold_en(static_cast<gpio_num_t>(config::LoraResetPin)) != ESP_OK) return fail();
    return true;
}

} // namespace geoputer

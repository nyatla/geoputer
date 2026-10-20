#include <Arduino.h>
#include "Application.h"
#include "BootMode.h"
#include "hardware/LoraSafety.h"
#include "utils/Console.h"
#include "task/storage/usb/UsbStorage.h"
#include <USB.h>
#include <esp_mac.h>
#include <tusb.h>

namespace {
geoputer::Application application;
bool ready = false;
bool usb_boot = false, console_reported = false;
}

void setup() {
    geoputer::holdUnusedLoraInReset(); // Before NVS, USB, UI, GPS or SD initialization.
    geoputer::BootMode mode;
    const bool valid_mode = geoputer::loadBootMode(mode);
    geoputer::Console.begin(115200);
    const bool usb_mode = mode == geoputer::BootMode::UsbStorage;
    usb_boot = usb_mode;
    USB.productName(usb_mode ? "GeoPuter SD" : "GeoPuter");
    // Start the USB transport before display/SD initialization in both modes.
    // MSC initially reports no medium; Storage publishes it once SD is ready.
    if (usb_mode) {
        geoputer::UsbStorage::registerInterface();
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        char serial[20];
        snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X-SD",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        USB.serialNumber(serial);
    }
    USB.begin();
    if (usb_mode) tud_disconnect();
    ready = application.begin(usb_mode);
    if (usb_mode) {
        // Enumerate once SD initialization has completed, even on NO SD, so
        // the host never caches the transient startup state as an empty drive.
        vTaskDelay(pdMS_TO_TICKS(150));
        tud_connect();
    }
    if (!valid_mode) geoputer::Console.println("[BOOT] invalid mode flag; using GPS logger");
    geoputer::Console.println(ready ? "[APP] selected boot mode started" : "[APP] initialization failed");
}

void loop() {
    if (!console_reported && geoputer::Console) {
        geoputer::Console.printf("[BOOT] mode=%s\n",usb_boot?"USB STORAGE":"GPS LOGGER");
        console_reported = true;
    }
    if (ready) application.runOnce();
    else vTaskDelay(pdMS_TO_TICKS(1000));
}

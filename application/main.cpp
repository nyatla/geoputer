#include <Arduino.h>
#include "ApplicationRuntime.h"
#include "BootMode.h"
#include "hardware/LoraSafety.h"
#include "utils/Console.h"
#include "task/storage/usb/UsbStorage.h"
#include <USB.h>
#include <esp_mac.h>
#include <esp_heap_caps.h>
#include <tusb.h>

namespace {
geoputer::ApplicationRuntime application;
bool ready = false;
bool usb_boot = false, wifi_boot = false, console_reported = false;
uint32_t memory_report_at = 0;
}

void setup() {
    geoputer::holdUnusedLoraInReset(); // Before NVS, USB, UI, GPS or SD initialization.
    geoputer::BootMode mode;
    const bool valid_mode = geoputer::loadBootMode(mode);
    geoputer::Console.begin(115200);
    const bool usb_mode = mode == geoputer::BootMode::UsbStorage;
    usb_boot = usb_mode;wifi_boot=mode==geoputer::BootMode::WifiManagement;
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
    ready = application.begin(mode);
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
    // Retry a retained snapshot after CDC attaches; early boot output may be lost.
    // No allocations, buffers or sampling task are needed for this report.
    const auto now = millis();
    if ((!console_reported || geoputer::config::DetailedDiagnostics) && now-memory_report_at>=10000) {
        memory_report_at=now;
        const auto written=geoputer::Console.printf(
            "[MODE-MEM] mode=%s fixed_gps=%u fixed_usb=%u fixed_wifi=%u union=%u app=%u sd_fil=%u heap=%u min_heap=%u largest=%u internal=%u stack_free=%u\n",
            wifi_boot?"WIFI":usb_boot?"USB":"GPS",
            static_cast<unsigned>(sizeof(geoputer::GpsApplication)),
            static_cast<unsigned>(sizeof(geoputer::UsbApplication)),
            static_cast<unsigned>(sizeof(geoputer::WifiApplication)),
            static_cast<unsigned>(sizeof(geoputer::ApplicationMemory)),
            static_cast<unsigned>(sizeof(geoputer::ApplicationRuntime)),
            static_cast<unsigned>(sizeof(geoputer::FIL)),ESP.getFreeHeap(),ESP.getMinFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            uxTaskGetStackHighWaterMark(nullptr));
        if(written)console_reported=true;
    }
    if (ready) application.runOnce();
    else vTaskDelay(pdMS_TO_TICKS(1000));
}

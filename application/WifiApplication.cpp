#include "Application.h"
#include "utils/Console.h"
#include <Arduino.h>
#include <tusb.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps_init.h>
extern "C" {extern char _bss_start[],_bss_end[];}
#endif
namespace geoputer {
bool Application::beginWifi() {
    if(!storage_.setWifiMode())return false;
    wifi_mode_=true;
#if defined(ARDUINO_ARCH_ESP32)
    // App is static on device. Wi-Fi never constructs the much larger GPS
    // member; donate only the disjoint inactive tail for this entire boot.
    const auto tail=mode_resources_.wifiHeapTail();
    if(tail.bytes() && tail.begin>=reinterpret_cast<uintptr_t>(_bss_start) &&
       tail.end<=reinterpret_cast<uintptr_t>(_bss_end)) {
        const auto result=heap_caps_add_region(tail.begin,tail.end);
        if(result==ESP_OK)mode_resources_.sealWifiHeapTail();
        Console.printf("[WIFI-HEAP] region_bytes=%u result=%d heap=%u\n",static_cast<unsigned>(tail.bytes()),result,ESP.getFreeHeap());
    } else Console.println("[WIFI-HEAP] static boot-lifetime tail unavailable");
#endif
    if(!notifications_.initialize() || !results_.initialize() || !ui_.initialize())return false;
    ui_.setPowerMode(BootMode::WifiManagement);
    if(!ui_.start("UI",8192,1))return false;
    data_.settings=defaultSettings();data_.revision=1;
    if(!setScreen(ScreenId::WifiManagement))return false;
    StorageRequest request{};request.kind=MessageKind::InitializeStorage;
    SyncReturn result{};
    if(!storage_.initialize() || !storage_.start("WifiStorage",12288,1) ||
       !callTask(TaskId::Storage,storage_.inbox(),request,result))return false;
    data_.storage=result.storage;data_.settings=result.storage.settings;++data_.revision;
    Console.println("[APP] Wi-Fi mode: GPS/logger/MSC disabled; Storage owns HTTP file operations");
    return publishUiContent();
}
void Application::runWifiOnce() {
    drainNotifications();
    if(restart_pending_) {
        publishUiContent();tud_disconnect();vTaskDelay(pdMS_TO_TICKS(150));ESP.restart();return;
    }
    if(ui_changed_)readUiContent();
    else if(ui_data_dirty_)publishUiContent();
    else {AppNotification event{};if(notifications_.receive(event,pdMS_TO_TICKS(50)))handleNotification(event);}
}
}

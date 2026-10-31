#include "ApplicationRuntime.h"
#include "utils/Console.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <esp_heap_caps_init.h>
extern "C" {extern char _bss_start[],_bss_end[];}
#endif
namespace geoputer {
bool ApplicationRuntime::begin(BootMode mode){
 if(memory_.selected()) return false;
 if(mode==BootMode::UsbStorage){memory_.selectUsb();return memory_.usb().begin();}
 if(mode==BootMode::WifiManagement){
  memory_.selectWifi();
#if defined(ARDUINO_ARCH_ESP32)
  // Only the unused tail after the COMPLETE live Wi-Fi application is donated.
  // The latch/tag lie outside this range. Reuse requires a hardware restart.
  const auto tail=memory_.wifiHeapTail();
  if(tail.bytes() && tail.begin>=reinterpret_cast<uintptr_t>(_bss_start) && tail.end<=reinterpret_cast<uintptr_t>(_bss_end)){
   const auto status=heap_caps_add_region(tail.begin,tail.end);
   if(status==ESP_OK)memory_.sealWifiHeapTail();
   Console.printf("[WIFI-HEAP] region_bytes=%u result=%d heap=%u\n",static_cast<unsigned>(tail.bytes()),status,ESP.getFreeHeap());
  }else Console.println("[WIFI-HEAP] static boot-lifetime tail unavailable");
#endif
  return memory_.wifi().begin();
 }
 memory_.selectGps();return memory_.gps().begin();
}
void ApplicationRuntime::runOnce(){
 if(memory_.isWifi())memory_.wifi().runOnce();
 else if(memory_.isUsb())memory_.usb().runOnce();
 else if(memory_.selected())memory_.gps().runOnce();
}
}

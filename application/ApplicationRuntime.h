#pragma once
#include "GpsApplication.h"
#include "apps/usb/UsbApplication.h"
#include "apps/wifi/WifiApplication.h"
#include "utils/BootModeResources.h"
namespace geoputer {
using ApplicationMemory=BootModeResources<GpsApplication,UsbApplication,WifiApplication>;
// Construct exactly one whole application before any of its tasks run.
// Destruction requires all tasks, callbacks and borrowers already stopped.
class ApplicationRuntime {
public:
 bool begin(BootMode mode);
 void runOnce();
private:
 ApplicationMemory memory_;
};
#if defined(ARDUINO_ARCH_ESP32)
// The dataset browser's six full titles and Wordbook type list use 221744 bytes on Cardputer ADV.
static_assert(sizeof(ApplicationRuntime)<=222000,"Whole application reservation exceeded the dataset browser budget");
#endif
}

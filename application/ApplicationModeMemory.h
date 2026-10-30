#pragma once
#include "AppModeResources.h"
#include "task/storage/StorageModeResources.h"
#include "utils/BootModeResources.h"
namespace geoputer {
// One outer union for the complete boot-mode reservation. App and Storage
// retain exclusive access to their own typed subobjects.
struct GpsApplicationResources {
    GpsApplicationResources(Queue<AppNotification,8>& events,Queue<ReturnMessage,1>& results,StorageObserver* observer)
        :app(events,results),storage(observer){}
    AppGpsResources app;
    StorageGpsResources storage;
};
struct UsbApplicationResources {
    explicit UsbApplicationResources(StorageObserver* observer):storage(observer){}
    AppUsbResources app;
    StorageUsbResources storage;
};
struct WifiApplicationResources {
    AppUsbResources app; // Only common screen ownership bookkeeping; no GPS resources.
    StorageWifiResources storage;
};
using ApplicationModeMemory=BootModeResources<GpsApplicationResources,UsbApplicationResources,WifiApplicationResources>;
static_assert(sizeof(ApplicationModeMemory)<sizeof(GpsApplicationResources)+sizeof(UsbApplicationResources),
              "Reserve the larger complete boot mode, never both modes");
} // namespace geoputer

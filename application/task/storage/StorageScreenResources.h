#pragma once
#include "MapBuildJob.h"
#include "log/LogReader.h"
namespace geoputer {
// Worker resources follow accepted jobs, not the current display. StorageTask
// admits only one read/map job and retains it through cancellation/completion.
struct StorageScreenResources {
    explicit StorageScreenResources(StorageObserver* observer=nullptr):logs("/sd/logs",observer){}
    MapBuildJob map;
    LogReader logs;
};
} // namespace geoputer

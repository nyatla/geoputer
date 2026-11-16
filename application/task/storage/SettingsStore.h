#pragma once

#include <Preferences.h>
#include "StorageTypes.h"

namespace geoputer {

// The boot path reads this once before USB initialization; GpsStorageTask owns
// later settings reads and writes. SD initialization is not a prerequisite.
class SettingsStore {
public:
    bool begin();
    bool available() const { return ready_; }
    SettingsState load(Settings& value, DatasetId& id);
    bool save(const Settings& value, const DatasetId& id);
    SettingsState load(Settings& value) { DatasetId unused{};return load(value,unused); }
    bool save(const Settings& value) { return save(value,DatasetId{}); }
private:
    Preferences preferences_;
    bool ready_ = false;
};

}  // namespace geoputer

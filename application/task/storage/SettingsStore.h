#pragma once

#include <Preferences.h>
#include "StorageTypes.h"

namespace geoputer {

// Accessed only by StorageTask. SD initialization is not a prerequisite.
class SettingsStore {
public:
    bool begin();
    bool available() const { return ready_; }
    SettingsState load(Settings& value);
    bool save(const Settings& value);
private:
    Preferences preferences_;
    bool ready_ = false;
};

}  // namespace geoputer

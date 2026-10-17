#include "SettingsStore.h"

namespace geoputer {

bool SettingsStore::begin() {
    if (!ready_) ready_ = preferences_.begin("geoputer", false);
    return ready_;
}

SettingsState SettingsStore::load(Settings& value) {
    value = defaultSettings();
    if (!ready_) return SettingsState::Unavailable;
    if (!preferences_.isKey("config")) return SettingsState::Defaults;
    uint8_t record[SettingsRecordSize]{};
    const auto size = preferences_.getBytesLength("config");
    if ((size != sizeof(record) && size != LegacySettingsRecordSize) ||
        preferences_.getBytes("config", record, size) != size ||
        !decodeSettings(record, size, value)) return SettingsState::Invalid;
    return SettingsState::Loaded;
}

bool SettingsStore::save(const Settings& value) {
    if (!ready_ || !validSettings(value)) return false;
    uint8_t record[SettingsRecordSize]{};
    encodeSettings(value, record);
    return preferences_.putBytes("config", record, sizeof(record)) == sizeof(record);
}

}  // namespace geoputer

#include "SettingsStore.h"

namespace geoputer {

bool SettingsStore::begin() {
    if (!ready_) ready_ = preferences_.begin("geoputer", false);
    return ready_;
}

SettingsState SettingsStore::load(Settings& value, DatasetId& id) {
    value = defaultSettings();
    id = {};
    if (!ready_) return SettingsState::Unavailable;
    if (!preferences_.isKey("config")) return SettingsState::Defaults;
    uint8_t record[SettingsRecordSize]{};
    const auto size = preferences_.getBytesLength("config");
    if ((size != sizeof(record) && size != PreviousSettingsRecordSize && size != LegacySettingsRecordSize) ||
        preferences_.getBytes("config", record, size) != size ||
        !decodeSettings(record, size, value, id)) return SettingsState::Invalid;
    return SettingsState::Loaded;
}

bool SettingsStore::save(const Settings& value, const DatasetId& id) {
    if (!ready_ || !validSettings(value) || !id.valid(true) || bool(value.dataset)!=bool(id)) return false;
    uint8_t record[SettingsRecordSize]{};
    encodeSettings(value, id, record);
    return preferences_.putBytes("config", record, sizeof(record)) == sizeof(record);
}

}  // namespace geoputer

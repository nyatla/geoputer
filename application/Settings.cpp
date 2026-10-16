#include "Settings.h"

namespace geoputer {

unsigned settingChoices(unsigned index) {
    constexpr unsigned counts[] = {2, 3, 5, 5, 5, 3, 2};
    return index < SettingCount ? counts[index] : 0;
}

uint8_t settingValue(const Settings& value, unsigned index) {
    switch (index) {
        case 0: return static_cast<uint8_t>(value.language);
        case 1: return value.log_interval;
        case 2: return value.flush_interval;
        case 3: return value.screen_timeout;
        case 4: return value.brightness;
        case 5: return value.dataset;
        case 6: return value.gps_mode;
        default: return UINT8_MAX;
    }
}

bool setSettingValue(Settings& value, unsigned index, uint8_t choice) {
    if (choice >= settingChoices(index)) return false;
    switch (index) {
        case 0: value.language = static_cast<UiLanguage>(choice); break;
        case 1: value.log_interval = choice; break;
        case 2: value.flush_interval = choice; break;
        case 3: value.screen_timeout = choice; break;
        case 4: value.brightness = choice; break;
        case 5: value.dataset = choice; break;
        case 6: value.gps_mode = choice; break;
        default: return false;
    }
    return true;
}

bool validSettings(const Settings& value) {
    for (unsigned i = 0; i < SettingCount; ++i) if (settingValue(value, i) >= settingChoices(i)) return false;
    return true;
}

bool sameSettings(const Settings& a, const Settings& b) {
    for (unsigned i = 0; i < SettingCount; ++i) if (settingValue(a, i) != settingValue(b, i)) return false;
    return true;
}

static uint32_t checksum(const uint8_t* bytes) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 12; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

void encodeSettings(const Settings& value, uint8_t (&record)[SettingsRecordSize]) {
    for (auto& byte : record) byte = 0;
    record[0] = 'G'; record[1] = 'P'; record[2] = 2; record[3] = SettingCount;
    // Keep the original six fields at their v1 offsets, regardless of menu order.
    for (unsigned i = 0; i < 6; ++i) record[4 + i] = settingValue(value, i + 1);
    record[10] = static_cast<uint8_t>(value.language);
    const auto sum = checksum(record);
    for (unsigned i = 0; i < 4; ++i) record[12 + i] = static_cast<uint8_t>(sum >> (i * 8));
}

bool decodeSettings(const uint8_t* record, size_t size, Settings& value) {
    if (!record || size != SettingsRecordSize || record[0] != 'G' || record[1] != 'P' || record[11]) return false;
    const bool legacy = record[2] == 1 && record[3] == 6 && record[10] == 0;
    if (!legacy && !(record[2] == 2 && record[3] == SettingCount)) return false;
    uint32_t sum = 0;
    for (unsigned i = 0; i < 4; ++i) sum |= static_cast<uint32_t>(record[12 + i]) << (i * 8);
    if (sum != checksum(record)) return false;
    Settings candidate = defaultSettings();
    for (unsigned i = 0; i < 6; ++i) if (!setSettingValue(candidate, i + 1, record[4 + i])) return false;
    if (!legacy && !setSettingValue(candidate, 0, record[10])) return false;
    value = candidate;  // Leave the caller's previous settings untouched on any failure.
    return true;
}

}  // namespace geoputer

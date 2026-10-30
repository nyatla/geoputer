#pragma once

#include <string_view>

#include <cstddef>
#include <cstdint>
#include "Config.h"
#include "power/PowerManager.h"

namespace geoputer {

enum class UiLanguage : uint8_t { Japanese, English };
enum class SettingId : uint8_t { Language, LogInterval, FlushInterval, ScreenTimeout, Brightness, Dataset, Volume, PowerSaving, GpsMode, Count };
constexpr unsigned SettingCount = static_cast<unsigned>(SettingId::Count);
constexpr unsigned OperationRowCount = static_cast<unsigned>(SettingId::GpsMode);
constexpr bool settingEditable(unsigned index) { return index < static_cast<unsigned>(SettingId::Dataset) || index==static_cast<unsigned>(SettingId::Volume) || index==static_cast<unsigned>(SettingId::PowerSaving); }

struct Settings {
    uint8_t log_interval;
    uint8_t flush_interval;
    uint8_t screen_timeout;
    uint8_t brightness;
    uint8_t dataset; // 0: unselected; 1..99: dataset directory number.
    uint8_t gps_mode;
    UiLanguage language;
    char wordbook_language[config::DatasetLanguageBytes]{}; // Empty means the dataset default at first boot.
    power::Level power_level=power::Level::None;
    uint8_t volume=1; // Shared system volume, UI levels 1..5.
};

constexpr Settings defaultSettings() { return {0, 2, 1, 1, 0, 0, UiLanguage::Japanese}; }
constexpr unsigned LogIntervals[] = {1, 5, 10};
constexpr unsigned FlushIntervals[] = {1, 5, 10, 30, 60};
// Zero disables automatic screen sleep; append it to preserve stored indices.
constexpr unsigned ScreenTimeouts[] = {5, 15, 30, 60, 0};
constexpr unsigned Brightnesses[] = {20, 40, 60, 80, 100};
constexpr size_t LegacySettingsRecordSize = 16;
constexpr size_t SettingsRecordSize = 48;
static_assert(config::DatasetLanguageBytes == 32, "NVS version 3 reserves 32 bytes for the language ID");
bool validDatasetLanguage(std::string_view value, bool allow_empty = false);
bool validDatasetLanguage(const char* value, bool allow_empty = false);

bool validSettings(const Settings& value);
uint8_t settingValue(const Settings& value, unsigned index);
bool setSettingValue(Settings& value, unsigned index, uint8_t choice);
unsigned settingChoices(unsigned index);
bool sameSettings(const Settings& a, const Settings& b);
void encodeSettings(const Settings& value, uint8_t (&record)[SettingsRecordSize]);
bool decodeSettings(const uint8_t* record, size_t size, Settings& value);

}  // namespace geoputer

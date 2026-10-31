#pragma once

#include <cstddef>
#include "GpsUiTypes.h"

namespace geoputer {

enum class SettingsLabel {
    FormatQuestion, ResetQuestion, FormatScope, ResetScope, KeepSd,
    Cancel, Confirm, ResetAction, CardSize, Total, Free, Unsupported, Working, Error
};

const char* settingsRootLabel(UiLanguage language, unsigned row);
const char* settingsModeValue(UiLanguage language, uint8_t mode = 0);
const char* settingsSystemLabel(UiLanguage language, unsigned row);
const char* systemModeBlockText(const UiData& data, bool changed, UiLanguage language);
const char* settingsOperationLabel(UiLanguage language, unsigned row);
const char* settingsStorageLabel(UiLanguage language, unsigned row);
const char* settingsLabel(UiLanguage language, SettingsLabel label);
const char* settingsCardState(UiLanguage language, CardState state);
const char* settingsMessage(UiLanguage language, const char* message);
void settingsValueText(UiLanguage language, unsigned row, uint8_t choice, char* out, size_t size);

}  // namespace geoputer

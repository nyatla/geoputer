#pragma once

#include "AlarmText.h"

namespace geoputer {

inline bool operationSettingEnabled(const UiData& data, unsigned row) {
    return row < OperationRowCount && !data.storage.busy &&
        settingEditable(operationSettingIndex(row));
}

inline bool alarmSettingEnabled(const UiData& data, unsigned row) {
    if (row >= AlarmRows || data.storage.busy || data.storage.nvs_error) return false;
    return row == AlarmReloadRow ? logMapAvailable(data) : data.storage.alarm.usable();
}

inline bool systemSettingEnabled(const UiData& data, uint8_t mode, unsigned row) {
    switch (row) {
        case 0: return !data.storage.busy;
        case 1: return false; // External GPS is not implemented.
        case 2: return mode != 0 && usbEntryAllowed(data);
        case SettingsSystemResetRow: return !data.storage.busy;
        default: return false;
    }
}

} // namespace geoputer

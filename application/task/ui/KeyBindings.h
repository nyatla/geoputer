#pragma once

#include "UiTypes.h"

namespace geoputer {

// Input is the unshifted value of a physical Cardputer key, independent of Fn.
// Del and Enter are supplied separately by the keyboard driver's state flags.
inline UiKey mapPhysicalKey(char key, bool /*fn*/) {
    switch (key) {
        case 's': return UiKey::ScreenOff;
        case 'g': return UiKey::GpsControl;
        case '0': return UiKey::OpenSettings;
        case '1': return UiKey::OpenLivePlace;
        case '2': return UiKey::OpenMap;
        case '3': return UiKey::OpenGpsLogger;
        case '4': return UiKey::OpenLogs;
        case 'd': return UiKey::DeleteLog;
        case ';': return UiKey::Up;
        case '.': return UiKey::Down;
        case ',': return UiKey::Left;
        case '/': return UiKey::Right;
        case '`': return UiKey::Escape;
        default: return UiKey::Other;
    }
}

// The printed angle brackets (Shift+, / Shift+.) also browse software notices.
// Use only in Licenses; plain period retains its Down binding.
inline UiKey mapLicenseKey(char key, bool fn, bool shift) {
    if (key == '<' || (shift && key == ',')) return UiKey::Left;
    if (key == '>' || (shift && key == '.')) return UiKey::Right;
    return mapPhysicalKey(key, fn);
}

}  // namespace geoputer

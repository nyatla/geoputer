#pragma once

#include "ScreenNavigation.h"

namespace geoputer {

// Input is the unshifted value of a physical Cardputer key, independent of Fn.
// Del and Enter are supplied separately by the keyboard driver's state flags.
inline UiKey mapPhysicalKey(char key, bool /*fn*/) {
    for (const auto& item : MenuScreens) if (item.shortcut == key) return item.key;
    switch (key) {
        case 's': return UiKey::ScreenOff;
        case 'g': return UiKey::GpsControl;
        case 'v': return UiKey::ToggleView;
        case 'm': return UiKey::LogMenu;
        case 'r': return UiKey::Recalculate;
        case '=': case '+': return UiKey::ZoomIn;
        case '-': return UiKey::ZoomOut;
        case 'b': return UiKey::FirstPoint;
        case 'e': return UiKey::LastPoint;
        case 'p': return UiKey::Playback;
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

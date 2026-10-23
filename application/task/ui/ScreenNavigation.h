#pragma once

#include "UiTypes.h"

namespace geoputer {

// Shared by UI dispatch and App validation. Child pages keep their own Back keys.
inline bool isScreenRoot(ScreenId screen, SettingsPage page = SettingsPage::Root) {
    return screen == ScreenId::LivePlace || screen == ScreenId::GpsLogger || screen == ScreenId::Logs ||
           (screen == ScreenId::Settings && page == SettingsPage::Root);
}

inline bool screenShortcut(UiKey key, ScreenId& target) {
    switch (key) {
        case UiKey::OpenLivePlace: target = ScreenId::LivePlace; return true;
        case UiKey::OpenGpsLogger: target = ScreenId::GpsLogger; return true;
        case UiKey::OpenLogs: target = ScreenId::Logs; return true;
        case UiKey::OpenSettings: case UiKey::Escape: target = ScreenId::Settings; return true;
        default: return false;
    }
}

inline bool rootScreenTransition(ScreenId from, SettingsPage page, ScreenId to) {
    return from != to && isScreenRoot(from, page) && isScreenRoot(to);
}

} // namespace geoputer

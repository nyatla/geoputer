#pragma once

#include "UiTypes.h"

namespace geoputer {

inline constexpr ScreenId MenuTargets[] = {ScreenId::LivePlace, ScreenId::Map, ScreenId::GpsLogger, ScreenId::Logs, ScreenId::Settings};
inline constexpr unsigned MenuCount = sizeof(MenuTargets) / sizeof(*MenuTargets);
inline const char* menuLabel(unsigned row, bool ja) {
    const char* en[] = {"1 Main", "2 Map", "3 GPS / Logger", "4 Logs", "0 Settings"};
    const char* jp[] = {"1 メイン", "2 地図", "3 GPS・記録", "4 ログ一覧", "0 設定"};
    return row < MenuCount ? (ja ? jp[row] : en[row]) : "";
}

// Shared by UI dispatch and App validation. Child pages keep their own Back keys.
inline bool isScreenRoot(ScreenId screen, SettingsPage page = SettingsPage::Root) {
    return screen == ScreenId::Menu || screen == ScreenId::Map || screen == ScreenId::LivePlace || screen == ScreenId::GpsLogger || screen == ScreenId::Logs ||
           (screen == ScreenId::Settings && page == SettingsPage::Root);
}

inline bool screenShortcut(UiKey key, ScreenId& target) {
    switch (key) {
        case UiKey::OpenLivePlace: target = ScreenId::LivePlace; return true;
        case UiKey::OpenGpsLogger: target = ScreenId::GpsLogger; return true;
        case UiKey::OpenMap: target = ScreenId::Map; return true;
        case UiKey::OpenLogs: target = ScreenId::Logs; return true;
        case UiKey::OpenSettings: target = ScreenId::Settings; return true;
        case UiKey::Escape: target = ScreenId::Menu; return true;
        default: return false;
    }
}

inline bool rootScreenTransition(ScreenId from, SettingsPage page, ScreenId to) {
    return from != to && isScreenRoot(from, page) && isScreenRoot(to);
}

} // namespace geoputer

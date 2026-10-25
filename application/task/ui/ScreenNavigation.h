#pragma once

#include "UiTypes.h"

namespace geoputer {

struct MenuScreen {
    ScreenId screen;
    UiKey key;
    char shortcut;
    const char* japanese;
    const char* english;
};
inline constexpr MenuScreen MenuScreens[] = {
    {ScreenId::LivePlace, UiKey::OpenLivePlace, '1', "1 メイン", "1 Main"},
    {ScreenId::Map, UiKey::OpenMap, '2', "2 地図", "2 Map"},
    {ScreenId::GpsLogger, UiKey::OpenGpsLogger, '3', "3 GPS・記録", "3 GPS / Logger"},
    {ScreenId::Logs, UiKey::OpenLogs, '4', "4 ログ一覧", "4 Logs"},
    {ScreenId::Settings, UiKey::OpenSettings, '0', "0 設定", "0 Settings"},
};
inline constexpr unsigned MenuCount = sizeof(MenuScreens) / sizeof(*MenuScreens);
inline const char* menuLabel(unsigned row, bool ja) {
    return row < MenuCount ? (ja ? MenuScreens[row].japanese : MenuScreens[row].english) : "";
}

// Shared by UI dispatch and App validation. Child pages keep their own Back keys.
inline bool isScreenRoot(ScreenId screen, SettingsPage page = SettingsPage::Root) {
    return screen == ScreenId::Menu || screen == ScreenId::Map || screen == ScreenId::LivePlace || screen == ScreenId::GpsLogger || screen == ScreenId::Logs ||
           (screen == ScreenId::Settings && page == SettingsPage::Root);
}

inline bool screenShortcut(UiKey key, ScreenId& target) {
    if (key == UiKey::Escape) { target = ScreenId::Menu; return true; }
    for (const auto& item : MenuScreens)
        if (item.key == key) { target = item.screen; return true; }
    return false;
}

inline bool rootScreenTransition(ScreenId from, SettingsPage page, ScreenId to) {
    return from != to && isScreenRoot(from, page) && isScreenRoot(to);
}

} // namespace geoputer

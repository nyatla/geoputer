#pragma once

#include "GpsUiTypes.h"

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

inline constexpr bool hasScreenName(ScreenId screen) {
    return screen==ScreenId::LivePlace || screen==ScreenId::Map ||
        screen==ScreenId::GpsLogger || screen==ScreenId::Logs;
}
inline const char* screenName(ScreenId screen) {
    switch(screen) {
        case ScreenId::LivePlace:return "1 LIVE";
        case ScreenId::Map:return "2 MAP";
        case ScreenId::GpsLogger:return "3 GPS / LOGGER";
        case ScreenId::Logs:return "4 LOGS";
        default:return "";
    }
}
inline constexpr bool hasRuntimeHeader(ScreenId screen) { return hasScreenName(screen); }
inline bool hasPlaybackHeader(ScreenId screen,const UiData& data) {
    return screen==ScreenId::LogPoint && data.browse().map_mode;
}

// Numeric keys select local views after a log file has been opened.
struct LogViewShortcut {UiKey key;BrowseAction action;};
inline constexpr LogViewShortcut LogViewShortcuts[]={
    {UiKey::OpenLivePlace,BrowseAction::Text},
    {UiKey::OpenMap,BrowseAction::Map},
    {UiKey::OpenGpsLogger,BrowseAction::Statistics},
};
inline bool isLogViewScreen(ScreenId screen) {
    return screen==ScreenId::LogProperties || screen==ScreenId::LogPoint || screen==ScreenId::LogStatistics;
}
inline bool selectedLogView(ScreenId screen,const UiData& data,BrowseAction action) {
    return action==BrowseAction::Statistics?screen==ScreenId::LogStatistics:
        screen==ScreenId::LogPoint && data.browse().map_mode==(action==BrowseAction::Map);
}
inline bool logViewAvailable(ScreenId screen,const UiData& data,BrowseAction action) {
    if(!isLogViewScreen(screen))return false;
    const auto& view=data.browse();
    if(view.loading || view.error!=LogReadError::None)return false;
    if(action==BrowseAction::Statistics)return true;
    return view.summary.records && (action!=BrowseAction::Map || logMapAvailable(data));
}

// Shared by UI dispatch and App validation. Child pages keep their own Back keys.
inline bool isScreenRoot(ScreenId screen, SettingsPage page = SettingsPage::Root) {
    return screen == ScreenId::Menu || screen == ScreenId::Map || screen == ScreenId::LivePlace || screen == ScreenId::GpsLogger || screen == ScreenId::Logs ||
           (screen == ScreenId::Settings && page == SettingsPage::Root);
}

inline bool screenShortcut(UiKey key, ScreenId& target) {
    for (const auto& item : MenuScreens)
        if (item.key == key) { target = item.screen; return true; }
    return false;
}

inline bool rootScreenTransition(ScreenId from, SettingsPage page, ScreenId to) {
    return from != to && isScreenRoot(from, page) && isScreenRoot(to);
}

} // namespace geoputer

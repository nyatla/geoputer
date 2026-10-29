#pragma once

#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include "../gps/GpsTypes.h"
#include "../storage/StorageTypes.h"
#include "../../BootStatus.h"
#include "../../map/MapTypes.h"

namespace geoputer {

enum class ScreenId : uint8_t { LivePlace, Settings, Boot, GpsLogger, Logs, LogPoint, UsbStorage, Map, Menu, LogProperties, LogStatistics };
inline bool isLogBrowser(ScreenId screen) { return screen==ScreenId::Logs || screen==ScreenId::LogPoint || screen==ScreenId::LogProperties || screen==ScreenId::LogStatistics; }
enum class DisplayMode : uint8_t { Background, View };
enum class LogAction : uint8_t { Start, Pause, Resume, Stop };
struct BatteryStatus { int8_t percent = -1; bool charging = false; bool charge_known = false; };

enum class ActionKind : uint8_t { None, ChangeScreen, SaveSettings, ResetSettings, Storage, Sleep, Wake, Logging, Browse, Dataset, UsbExport, Maintenance, RestartLogger, MapScale };
enum class UsbPage : uint8_t { Menu, WaitEject, Files, Format, Progress, Complete, Error, Busy };
enum class UsbAction : uint8_t { Begin, Start, Publish, Older, Newer };
enum class BrowseAction : uint8_t { OpenFile, Older, Newer, Reload, Point, Delete, ToggleView, Playback, PausePlayback, OpenPoint, Text, Map, Statistics, Recalculate };
enum class ActionResult : uint8_t { None, Applied, Rejected };
enum class UiKey : uint8_t { OpenGpsLogger, OpenSettings, OpenLogs, DeleteLog, Escape, Up, Down, Left, Right, Enter, Other, Back, OpenLivePlace, OpenMap, ScreenOff, GpsControl, ToggleView, ZoomIn, FirstPoint, LastPoint, Playback, ZoomOut, LogMenu, Recalculate };
enum class SettingsPage : uint8_t { Root, Operation, Storage, Reset, Licenses, DatasetList, DatasetDetail, About, DatasetIndex, DatasetMenu, System };
// Menu commands are separate from persisted settings and page identifiers.
enum class SettingsRootItem : uint8_t { Operation, System, Storage, About, Licenses, Count };
constexpr unsigned settingsRootIndex(SettingsRootItem item) { return static_cast<unsigned>(item); }
constexpr unsigned SettingsRootCount = settingsRootIndex(SettingsRootItem::Count);
constexpr unsigned SettingsSystemRow = settingsRootIndex(SettingsRootItem::System);
constexpr unsigned SettingsSystemResetRow = 3;
constexpr unsigned SettingsSystemCount = 4;

struct UiAction {
    ActionKind kind;
    uint32_t action_id;
    uint32_t screen_generation;
    ScreenId target;
    Settings settings;
    StorageOperation storage_operation;
    bool confirmed;
    LogAction log_action;
    BrowseAction browse_action;
    DatasetAction dataset_action;
    DatasetPurpose dataset_purpose;
    uint8_t dataset_number=0;
    uint32_t browse_index, browse_revision;
    UsbAction usb_action;
    ExportFormat export_format;
    uint8_t map_scale;
};

// Fixed-size value copies. No pointers into another task's mutable data.
struct UiData {
    uint32_t revision;
    map::ViewState map;
    BootStatus boot;
    char place[2][64];  // Final display rows formatted/cached by Storage.
    PlaceState place_state;
    DatasetInfo dataset;
    GpsFix place_fix;
    map::RegionCode place_region=0;
    GpsSnapshot gps;
    Settings settings;
    StorageSnapshot storage;
    RecordingSnapshot recording;
    char message[40];
    uint32_t message_at_ms;

    // Only one page payload is retained. Readers never activate a different
    // member; an unused page reads as empty. Owners explicitly select on write.
    void copyFrom(const UiData& other,bool keep_datasets) {
        if(!keep_datasets || !hasDatasets()) {*this=other;return;}
        revision=other.revision;map=other.map;boot=other.boot;
        std::memcpy(place,other.place,sizeof(place));place_state=other.place_state;dataset=other.dataset;
        place_fix=other.place_fix;place_region=other.place_region;gps=other.gps;settings=other.settings;
        storage=other.storage;recording=other.recording;std::memcpy(message,other.message,sizeof(message));message_at_ms=other.message_at_ms;
    }
    void clearPage() { page_kind_ = PageKind::None; }
    bool hasBrowse() const { return page_kind_ == PageKind::Browse; }
    bool hasDatasets() const { return page_kind_ == PageKind::Datasets; }
    const LogBrowserView& browse() const { return hasBrowse() ? page_.browse : EmptyBrowse; }
    const DatasetView& datasets() const { return hasDatasets() ? page_.datasets : EmptyDatasets; }
    LogBrowserView& editBrowse() {
        if (!hasBrowse()) {
            new (&page_.browse) LogBrowserView{};
            page_kind_ = PageKind::Browse;
        }
        return page_.browse;
    }
    DatasetView& editDatasets() {
        if (!hasDatasets()) {
            new (&page_.datasets) DatasetView{};
            page_kind_ = PageKind::Datasets;
        }
        return page_.datasets;
    }

private:
    enum class PageKind : uint8_t { None, Browse, Datasets };
    union PageData {
        LogBrowserView browse;
        DatasetView datasets;
        constexpr PageData() : browse{} {}
    } page_{};
    PageKind page_kind_ = PageKind::None;
    inline static constexpr LogBrowserView EmptyBrowse{};
    inline static constexpr DatasetView EmptyDatasets{};
};

static_assert(std::is_trivially_copyable<UiData>::value, "UI page data must remain safe for value-copy queues");

inline bool viewingLogPoint(ScreenId screen) { return screen==ScreenId::LogPoint; }
inline bool showingMap(ScreenId screen,const UiData& data) {
    return screen==ScreenId::Map || (viewingLogPoint(screen) && data.browse().map_mode);
}
inline const GpsFix& displayedFix(ScreenId screen,const UiData& data) {
    return viewingLogPoint(screen)?data.browse().point:data.gps.fix;
}
inline bool logMapAvailable(const UiData& data) {
    return data.storage.card_state==CardState::Mounted && data.settings.dataset && data.dataset.state==PlaceState::Ready;
}
inline bool usbEntryAllowed(const UiData& data) {
    return loggerStopped(data.recording, data.storage.log) && !data.storage.busy &&
        !data.datasets().loading && !data.datasets().applying && data.storage.settings_state != SettingsState::Unavailable;
}

struct UiSnapshot {
    ScreenId screen;
    uint32_t screen_generation;
    uint32_t content_revision;
    uint32_t last_input_ms;
    uint8_t selected_setting;
    UiAction action;
    SettingsPage page;
};

struct SetUiScreen {
    ScreenId screen;
    DisplayMode mode;
    uint32_t generation;
    UiData data;
    uint32_t processed_action_id;
    ActionResult action_result;
};

struct UpdateUiContent {
    DisplayMode mode;
    uint32_t generation;
    UiData data;
    uint32_t processed_action_id;
    ActionResult action_result;
};

}  // namespace geoputer

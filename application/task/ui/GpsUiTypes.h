#pragma once

#include <cstdint>
#include "UiPrimitives.h"
#include <cstring>
#include <new>
#include <type_traits>
#include "../../Config.h"
#include "../gps/GpsTypes.h"
#include "../storage/StorageTypes.h"
#include "../../BootStatus.h"
#include "../../map/MapTypes.h"

namespace geoputer {

inline bool isLogBrowser(ScreenId screen) { return screen==ScreenId::Logs || screen==ScreenId::LogPoint || screen==ScreenId::LogProperties || screen==ScreenId::LogStatistics; }

enum class ActionKind : uint8_t { None, ChangeScreen, SaveSettings, ResetSettings, Storage, Sleep, Wake, Logging, Browse, Dataset, Maintenance, RestartLogger, MapScale, SaveAlarm, ReloadAlarm, PreviewAlarm, SilenceAlarm, BootSetup };
enum class BrowseAction : uint8_t { OpenFile, Older, Newer, Reload, Point, Delete, Playback, PausePlayback, OpenPoint, Text, Map, Statistics, Recalculate, ReplayFaster, ReplaySlower, ReplayForward, ReplayReverse };
enum class SettingsPage : uint8_t { Root, Operation, Storage, Reset, Licenses, DatasetList, DatasetChoice, DatasetDetail, About, DatasetIndex, DatasetMenu, System, Alarm };
inline constexpr bool isDatasetBrowserPage(SettingsPage page) {
    return page == SettingsPage::DatasetList || page == SettingsPage::DatasetChoice ||
        page == SettingsPage::DatasetDetail;
}
inline constexpr bool isDatasetSettingsPage(SettingsPage page) {
    return isDatasetBrowserPage(page) || page == SettingsPage::DatasetMenu ||
        page == SettingsPage::DatasetIndex;
}
// Menu commands are separate from persisted settings and page identifiers.
enum class SettingsRootItem : uint8_t { Operation, System, Alarm, Dataset, Storage, About, Licenses, Count };
constexpr unsigned settingsRootIndex(SettingsRootItem item) { return static_cast<unsigned>(item); }
constexpr unsigned SettingsRootCount = settingsRootIndex(SettingsRootItem::Count);
constexpr unsigned SettingsSystemRow = settingsRootIndex(SettingsRootItem::System);
constexpr unsigned SettingsSystemResetRow = 3;
constexpr unsigned SettingsSystemCount = 4;
constexpr unsigned SettingsStorageCount = 4;
constexpr unsigned DatasetMenuRows = 3;

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
    DatasetId dataset_id{};
    uint16_t dataset_page=0;
    uint32_t browse_index, browse_revision;
    uint8_t map_scale;
    alarm::Settings alarm_settings{};
    uint32_t alarm_identity=0;
};

// Fixed-size value copies. No pointers into another task's mutable data.
struct UiData {
    uint32_t revision;
    map::ViewState map;
    BootStatus boot;
    char place[2][config::PlaceCachedLineBytes];  // Logical rows; each screen lays them out.
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

#pragma once

#include <cstdint>
#include "../gps/GpsTypes.h"
#include "../storage/StorageTypes.h"
#include "../../BootStatus.h"

namespace geoputer {

enum class ScreenId : uint8_t { LivePlace, Settings, Boot, Menu, GpsLogger, Logs, LogDetail, LogPoint };
inline bool isLogBrowser(ScreenId screen) { return screen==ScreenId::Logs || screen==ScreenId::LogDetail || screen==ScreenId::LogPoint; }
enum class DisplayMode : uint8_t { Background, View };
enum class LogAction : uint8_t { Start, Pause, Resume, Stop };
struct BatteryStatus { int8_t percent = -1; bool charging = false; bool charge_known = false; };

enum class ActionKind : uint8_t { None, ChangeScreen, SaveSettings, ResetSettings, Storage, Sleep, Wake, Logging, Browse };
enum class BrowseAction : uint8_t { OpenFile, Older, Newer, Points, Reload, Point };
enum class ActionResult : uint8_t { None, Applied, Rejected };
enum class UiKey : uint8_t { Menu, Escape, Up, Down, Left, Right, Enter, Other, F1 };
enum class SettingsPage : uint8_t { Root, Operation, Storage, Reset, Licenses };

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
    uint32_t browse_index, browse_revision;
};

// Fixed-size value copies. No pointers into another task's mutable data.
struct UiData {
    uint32_t revision;
    BootStatus boot;
    char place[2][64];  // App-formatted address; Storage retains three hierarchy rows.
    PlaceState place_state;
    DatasetInfo dataset;
    GpsFix place_fix;
    GpsSnapshot gps;
    Settings settings;
    StorageSnapshot storage;
    RecordingSnapshot recording;
    char message[40];
    uint32_t message_at_ms;
    LogBrowserView browse;
};

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

#pragma once

#include <cstdint>
#include <type_traits>
#include "task/ui/UiTypes.h"

namespace geoputer {

enum class TaskId : uint8_t { Application, UI, GPS, Storage };
enum class MessageKind : uint8_t {
    SetUiScreen,
    UpdateUiContent,
    GetUiContent,
    UiContentChanged,
    StartGpsReception,
    GetGpsStatus,
    GpsSampleReady,
    GpsStatusChanged,
    InitializeStorage,
    GetStorageStatus,
    SaveSettings,
    OperateStorage,
    LoadSettings,
    SearchPlace,
    CheckDataset,
    RgCacheStateChanged,
    SdAccessChanged,
    OpenLogSession,
    FlushLog,
    CloseLogSession,
    ListLogs,
    ReadLogSummary,
    ReadLogRecords,
    DeleteLog,
    CancelLogRead,
    LogWritten,
    SettingsWritten,
    ReadCompleted,
    ReadFailed,
    StorageOperationCompleted,
    StorageError,
    DatasetOperation,
    DatasetProgress,
    UsbStorageChanged,
    UsbExportOperation,
    RequestMap, ReleaseMap, CancelMap, MapCompleted,
    BeginMapFrame, CommitMapFrame, DiscardMapFrame, CloseMapSession,
    SyncReturn,
    ReleaseLogBrowse, ReadLogStatistics, LogStatisticsProgress,
    BeginPlaceSearch, CommitPlaceSearch, DiscardPlaceSearch,
};
enum class OperationStatus : uint8_t { Ok, InvalidRequest, StaleContent, Unavailable };

struct SyncReturn {
    MessageKind request_kind;
    OperationStatus status;
    // Active member is selected by source and request_kind in prepareSyncReturn.
    // BeginMapFrame/BeginPlaceSearch return a UI back lease; Storage returns its snapshot.
    union {
        UiSnapshot content;
        GpsSnapshot gps;
        StorageSnapshot storage;
        map::BufferLease map_buffer;
    };
};

struct UiContentChanged {
    uint32_t revision;
    uint32_t screen_generation;
};

// The envelope and queue counts are common; payload storage is direction-specific.
struct MessageHeader {
    MessageKind kind;
    TaskId source;
    uint32_t request_id;
    uint32_t sequence;
    uint32_t sent_at_ms;
};

template <typename Payload>
struct Message : MessageHeader { Payload payload; };

union UiRequestPayload {
    SetUiScreen screen;
    UpdateUiContent update;
    map::FrameInfo map;
    PlaceRequest place;
};
union StorageRequestPayload {
    StorageCommand storage_command;
    PlaceRequest place_request;
    LogCommand log;
    LogReadRequest log_read;
    DatasetRequest dataset;
    ExportRequest export_request;
    map::Request map;
};
union AppNotificationPayload {
    map::FrameInfo map;
    IndexProgress index_progress;
    LogStatisticsProgress log_progress;
    StorageSnapshot usb_storage;
    UiContentChanged changed;
    GpsSnapshot gps;
    StorageCompletion storage_completion;
    RgCacheSnapshot rg;
    SdAccessSnapshot sd;
};
struct ReturnPayload { SyncReturn result; };

using UiRequest = Message<UiRequestPayload>;
using StorageRequest = Message<StorageRequestPayload>;
// Current GPS commands have no arguments.
struct GpsRequest : MessageHeader {};
using AppNotification = Message<AppNotificationPayload>;
using ReturnMessage = Message<ReturnPayload>;

static_assert(std::is_trivially_copyable<UiRequest>::value &&
              std::is_trivially_copyable<StorageRequest>::value &&
              std::is_trivially_copyable<GpsRequest>::value &&
              std::is_trivially_copyable<AppNotification>::value &&
              std::is_trivially_copyable<ReturnMessage>::value,
              "Task messages must be safe to copy through FreeRTOS queues");
static_assert(sizeof(GpsRequest) == sizeof(MessageHeader), "GPS requests have no payload");
static_assert(sizeof(StorageRequest) < sizeof(UiRequest) && sizeof(ReturnMessage) < sizeof(UiRequest) &&
              sizeof(AppNotification) < sizeof(UiRequest), "Only UI requests carry full UI content");

}  // namespace geoputer

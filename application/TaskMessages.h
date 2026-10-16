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
    CancelLogRead,
    LogWritten,
    SettingsWritten,
    ReadCompleted,
    ReadFailed,
    StorageOperationCompleted,
    StorageError,
    SyncReturn,
};
enum class OperationStatus : uint8_t { Ok, InvalidRequest, StaleContent, Unavailable };

struct SyncReturn {
    MessageKind request_kind;
    OperationStatus status;
    UiSnapshot content;
    GpsSnapshot gps;
    StorageSnapshot storage;
};

struct UiContentChanged {
    uint32_t revision;
    uint32_t screen_generation;
};

struct TaskMessage {
    MessageKind kind;
    TaskId source;
    uint32_t request_id;
    uint32_t sequence;
    uint32_t sent_at_ms;
    union {
        SetUiScreen screen;
        UpdateUiContent update;
        SyncReturn result;
        UiContentChanged changed;
        GpsSnapshot gps;
        StorageCommand storage_command;
        StorageCompletion storage_completion;
        PlaceRequest place_request;
        RgCacheSnapshot rg;
        SdAccessSnapshot sd;
        LogCommand log;
        LogReadRequest log_read;
    } payload;
};

static_assert(std::is_trivially_copyable<TaskMessage>::value,
              "Task messages must be safe to copy through FreeRTOS queues");

}  // namespace geoputer

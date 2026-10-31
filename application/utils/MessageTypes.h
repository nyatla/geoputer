#pragma once
#include <cstdint>
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
    WifiStorageChanged, StopWifi, RegenerateWifi,
    UsbStorageChanged,
    UsbExportOperation,
    RequestMap, ReleaseMap, CancelMap, MapCompleted,
    BeginMapFrame, CommitMapFrame, DiscardMapFrame, CloseMapSession,
    SyncReturn,
    ReleaseLogBrowse, ReadLogStatistics, LogStatisticsProgress,
    BeginPlaceSearch, CommitPlaceSearch, DiscardPlaceSearch,
    BeginDatasetRead, CommitDatasetRead, DiscardDatasetRead, DatasetReadError, UpdateDatasetProgress, ControlAudio,
};
enum class OperationStatus : uint8_t { Ok, InvalidRequest, StaleContent, Unavailable };
struct MessageHeader {
    MessageKind kind;
    TaskId source;
    uint32_t request_id;
    uint32_t sequence;
    uint32_t sent_at_ms;
};
}

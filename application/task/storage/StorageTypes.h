#pragma once

#include <cstdint>
#include "../../Settings.h"
#include "PlaceTypes.h"
#include "StorageActivity.h"
#include "log/LogTypes.h"
#include "log/LogReadTypes.h"
#include "dataset/DatasetTypes.h"
#include "usb/UsbStorageTypes.h"
#include "export/ExportTypes.h"

namespace geoputer {

enum class SettingsState : uint8_t { Unavailable, Defaults, Loaded, Invalid, Saving, Saved, Failed };
enum class CardState : uint8_t { Unknown, NoCard, Mounted, Unmounted, Unsupported, Failed };
enum class StorageOperation : uint8_t { None, SaveSettings, Remount, Unmount, Format, EnterUsbMode, ExitUsbMode };
enum class StorageReadKind : uint8_t { None, Settings, Place, Dataset, Log, LogBrowse, DatasetBrowser };

struct StorageSnapshot {
    uint32_t revision;
    SettingsState settings_state;
    Settings settings;
    DatasetFailure dataset_failure; // Latest automatic release; not persisted across reboot.
    CardState card_state;
    uint8_t fat_type;
    uint64_t card_bytes;
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool busy;
    StorageOperation operation;
    bool operation_ok;
    int32_t error;
    int32_t nvs_error;
    int32_t sd_error;
    RgCacheSnapshot rg;
    SdAccessSnapshot sd;
    LogSnapshot log;
    LogError log_result;
    UsbStorageView usb;
    ExportSnapshot export_job;
};

struct StorageCommand {
    StorageOperation operation;
    Settings settings;
    uint32_t operation_id;
    bool confirmed;
};

struct StorageCompletion {
    uint32_t request_id;
    uint32_t operation_id;
    StorageSnapshot snapshot;
    StorageReadKind read_kind;
    union { PlaceResult place; LogReadReply logs; DatasetView dataset_view; };
};

inline const char* cardStateName(CardState state) {
    switch (state) {
        case CardState::Unknown: return "WAITING";
        case CardState::NoCard: return "NO CARD";
        case CardState::Mounted: return "MOUNTED";
        case CardState::Unmounted: return "SAFE TO REMOVE";
        case CardState::Unsupported: return "FAT32 REQUIRED";
        case CardState::Failed: return "SD ERROR";
    }
    return "SD ERROR";
}

}  // namespace geoputer

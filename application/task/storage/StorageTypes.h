#pragma once

#include <cstdint>
#include "StorageBaseTypes.h"
#include "../../Settings.h"
#include "PlaceTypes.h"
#include "StorageActivity.h"
#include "log/LogTypes.h"
#include "log/LogReadTypes.h"
#include "dataset/DatasetTypes.h"

namespace geoputer {

enum class StorageReadKind : uint8_t { None, Settings, Place, Dataset, Log, LogBrowse, DatasetBrowser };

struct StorageSnapshot {
    uint32_t revision;
    SettingsState settings_state;
    Settings settings;
    DatasetId selected_dataset; // Exact active directory ID; Settings::dataset is only a boolean.
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
    alarm::Profile alarm{};
};

struct StorageCommand {
    StorageOperation operation;
    Settings settings;
    uint32_t operation_id;
    bool confirmed;
    alarm::Settings alarm_settings{};
    uint32_t alarm_identity=0;
};

// Text and diagnostic strings stay in the borrowed UI result, never the queue.
struct PlaceCompletion {
    PlaceRequest request;
    PlaceState state;
    int64_t region_code;
    DatasetInfo dataset;
};
inline PlaceCompletion placeCompletion(const PlaceResult& result) {
    return {result.request,result.state,result.region_code,result.dataset};
}

struct StorageCompletion {
    uint32_t request_id;
    uint32_t operation_id;
    StorageSnapshot snapshot;
    StorageReadKind read_kind;
    union { PlaceCompletion place; LogReadReply logs; DatasetCompletion dataset; };
};


}  // namespace geoputer

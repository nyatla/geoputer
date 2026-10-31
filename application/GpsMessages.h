#pragma once

#include <cstdint>
#include "utils/MessageTypes.h"
#include <type_traits>
#include "task/ui/GpsUiTypes.h"

namespace geoputer {


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

template <typename Payload>
struct Message : MessageHeader { Payload payload; };

union UiRequestPayload {
    SetUiScreen screen;
    UpdateUiContent update;
    map::FrameInfo map;
    PlaceRequest place;
    DatasetCompletion dataset;
    IndexProgress dataset_progress;
    alarm::AudioCommand audio;
};
union StorageRequestPayload {
    StorageCommand storage_command;
    PlaceRequest place_request;
    LogCommand log;
    LogReadRequest log_read;
    DatasetRequest dataset;
    map::Request map;
};
union AppNotificationPayload {
    map::FrameInfo map;
    IndexProgress index_progress;
    LogStatisticsProgress log_progress;
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

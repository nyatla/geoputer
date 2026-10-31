#pragma once
#include "SyncClientBase.h"
#include "../GpsMessages.h"
#include <new>
namespace geoputer {
// Initialize the task-owned reply in place; do not allocate another return frame.
inline void prepareSyncReturn(ReturnMessage& reply, TaskId source, const MessageHeader& request,
                              uint32_t sequence, OperationStatus status = OperationStatus::InvalidRequest) {
    reply = {};
    reply.kind = MessageKind::SyncReturn;
    reply.source = source;
    reply.request_id = request.request_id;
    reply.sequence = sequence;
    reply.sent_at_ms = taskTimeMs();
    reply.payload.result.request_kind = request.kind;
    auto& result = reply.payload.result;
    result.status = status;
    // These snapshots have default member initializers. Explicit construction
    // starts the selected union member's lifetime without another stack frame.
    if (source == TaskId::UI && (request.kind == MessageKind::BeginMapFrame || request.kind == MessageKind::BeginPlaceSearch || request.kind == MessageKind::BeginDatasetRead)) new (&result.map_buffer) map::BufferLease{};
    else if (source == TaskId::Storage) new (&result.storage) StorageSnapshot{};
    else if (source == TaskId::GPS) new (&result.gps) GpsSnapshot{};
}

using SyncClient=BasicSyncClient<SyncReturn,ReturnMessage>;
}

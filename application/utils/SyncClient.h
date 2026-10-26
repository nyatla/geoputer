#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>
#include "Queue.h"
#include "../TaskMessages.h"

namespace geoputer {

enum class SyncSendResult { AwaitingResult, QueueTimeout, DestinationUnavailable, SyncPendingError };
enum class SyncReceiveResult { Returned, ResultTimeout, NoPendingSyncError, ProtocolError };

inline uint32_t taskTimeMs() {
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

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
    if (source == TaskId::UI && (request.kind == MessageKind::BeginMapFrame || request.kind == MessageKind::BeginPlaceSearch)) new (&result.map_buffer) map::BufferLease{};
    else if (source == TaskId::Storage) new (&result.storage) StorageSnapshot{};
    else if (source == TaskId::GPS) new (&result.gps) GpsSnapshot{};
}

// Only the Application task uses this object. There is exactly one pending request.
class SyncClient {
public:
    explicit SyncClient(Queue<ReturnMessage, 1>& results) : results_(results) {}

    template <typename Request, size_t Capacity>
    SyncSendResult sendSync(TaskId peer, Queue<Request, Capacity>& destination,
                            Request& request, TickType_t timeout) {
        if (waiting_) return SyncSendResult::SyncPendingError;
        if (!results_.available() || !destination.available()) return SyncSendResult::DestinationUnavailable;
        // Stamp the caller-owned envelope in place. FreeRTOS copies the value
        // on send; passing a full UI payload by value wastes task stack space.
        request.source = TaskId::Application;
        request.request_id = nextId();
        request.sequence = ++sequence_;
        request.sent_at_ms = taskTimeMs();
        if (!destination.send(request, timeout)) return SyncSendResult::QueueTimeout;
        peer_ = peer;
        kind_ = request.kind;
        request_id_ = request.request_id;
        waiting_ = true;
        return SyncSendResult::AwaitingResult;
    }

    SyncReceiveResult receiveSyncResult(SyncReturn& result, TickType_t timeout) {
        if (!waiting_) return SyncReceiveResult::NoPendingSyncError;
        ReturnMessage message{};
        if (!results_.receive(message, timeout)) return SyncReceiveResult::ResultTimeout;
        if (message.kind != MessageKind::SyncReturn || message.source != peer_ ||
            message.request_id != request_id_ || message.payload.result.request_kind != kind_) {
            return SyncReceiveResult::ProtocolError;
        }
        result = message.payload.result;
        waiting_ = false;  // A terminal failure also completes result retrieval.
        return SyncReceiveResult::Returned;
    }

    bool waiting() const { return waiting_; }

private:
    uint32_t nextId() {
        if (++next_id_ == 0) ++next_id_;
        return next_id_;
    }

    Queue<ReturnMessage, 1>& results_;
    bool waiting_ = false;
    TaskId peer_ = TaskId::UI;
    MessageKind kind_ = MessageKind::GetUiContent;
    uint32_t request_id_ = 0;
    uint32_t next_id_ = 0;
    uint32_t sequence_ = 0;
};

}  // namespace geoputer

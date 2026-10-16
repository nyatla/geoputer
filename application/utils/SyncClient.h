#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "Queue.h"
#include "../TaskMessages.h"

namespace geoputer {

enum class SyncSendResult { AwaitingResult, QueueTimeout, DestinationUnavailable, SyncPendingError };
enum class SyncReceiveResult { Returned, ResultTimeout, NoPendingSyncError, ProtocolError };

inline uint32_t taskTimeMs() {
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// Only the Application task uses this object. There is exactly one pending request.
class SyncClient {
public:
    explicit SyncClient(Queue<TaskMessage, 1>& results) : results_(results) {}

    template <size_t Capacity>
    SyncSendResult sendSync(TaskId peer, Queue<TaskMessage, Capacity>& destination,
                            TaskMessage request, TickType_t timeout) {
        if (waiting_) return SyncSendResult::SyncPendingError;
        if (!results_.available() || !destination.available()) return SyncSendResult::DestinationUnavailable;
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
        TaskMessage message{};
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

    Queue<TaskMessage, 1>& results_;
    bool waiting_ = false;
    TaskId peer_ = TaskId::UI;
    MessageKind kind_ = MessageKind::GetUiContent;
    uint32_t request_id_ = 0;
    uint32_t next_id_ = 0;
    uint32_t sequence_ = 0;
};

}  // namespace geoputer

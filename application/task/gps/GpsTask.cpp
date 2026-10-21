#include "../../utils/Console.h"
#include "GpsTask.h"
#include "../../utils/SyncClient.h"

namespace geoputer {

void GpsTask::handle(const GpsRequest& request) {
    if (request.source != TaskId::Application || request.request_id == 0) return;
    prepareSyncReturn(pending_return_, TaskId::GPS, request, ++sequence_);
    auto& result = pending_return_.payload.result;
    if (request.kind == MessageKind::StartGpsReception) {
        if (!ready_) {
            // Fixed Mesh Kit wiring. No GNSS configuration/reset commands or LoRa initialization.
            const bool buffer_ready = serial_.setRxBufferSize(2048) != 0;
            if (buffer_ready) serial_.begin(115200, SERIAL_8N1, 15, 13);
            ready_ = buffer_ready && static_cast<bool>(serial_);
            if (ready_) receiver_.begin(taskTimeMs());
            Console.printf("[GPS] UART %s RX=15 TX=13 baud=115200\n", ready_ ? "ready" : "failed");
        }
        result.status = ready_ ? OperationStatus::Ok : OperationStatus::Unavailable;
    } else if (request.kind == MessageKind::GetGpsStatus) {
        result.status = ready_ ? OperationStatus::Ok : OperationStatus::Unavailable;
    }
    result.gps = receiver_.snapshot();
    if (!ready_) result.gps.reception = GpsReception::Unavailable;
    has_return_ = true;
}

void GpsTask::sendReturn() {
    if (has_return_ && sendSpeculative(results_, pending_return_) == SpeculativeResult::Queued) has_return_ = false;
    // Retain the completed value on queue-full; reception continues without repeating the operation.
}

void GpsTask::notify() {
    if (!receiver_.changed()) return;
    AppNotification event{};
    event.kind = receiver_.sampleChanged() ? MessageKind::GpsSampleReady : MessageKind::GpsStatusChanged;
    event.source = TaskId::GPS;
    event.sequence = ++sequence_;
    event.sent_at_ms = taskTimeMs();
    event.payload.gps = receiver_.snapshot();
    if (failed_revision_ && failed_revision_ != event.payload.gps.revision) {
        ++coalesced_;  // Display-only stage: retain the latest snapshot rather than accumulate a log.
        failed_revision_ = 0;
    }
    if (sendSpeculative(notifications_, event) == SpeculativeResult::Queued) {
        failed_revision_ = 0;
        receiver_.notificationSent();
    } else {
        failed_revision_ = event.payload.gps.revision;
    }
}

void GpsTask::run() {
    TickType_t wake = xTaskGetTickCount();
    uint32_t received_bytes = 0;
    uint32_t diagnostic_at = taskTimeMs();
    for (;;) {
        sendReturn();
        GpsRequest request{};
        if (!has_return_ && inbox().receive(request)) {
            handle(request);
            sendReturn();
        }
        if (ready_) {
            // Never wait for a fix. Bound UART work to revisit commands and return retries regularly.
            const uint32_t now = taskTimeMs();
            for (unsigned bytes = 0; bytes < 1024 && serial_.available(); ++bytes) {
                const int byte = serial_.read();
                if (byte >= 0) {
                    ++received_bytes;
                    receiver_.feed(static_cast<char>(byte), now);
                }
            }
            receiver_.poll(now);
            notify();
            if (now - diagnostic_at >= 5000) {
                diagnostic_at = now;
                Console.printf("[GPS] RX=%lu NMEA=%lu rejected=%lu coalesced=%lu state=%s\n",
                              static_cast<unsigned long>(received_bytes),
                              static_cast<unsigned long>(receiver_.receivedSentences()),
                              static_cast<unsigned long>(receiver_.rejectedSentences()),
                              static_cast<unsigned long>(coalesced_), gpsReceptionName(receiver_.snapshot().reception));
            }
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(5));
    }
}

}  // namespace geoputer

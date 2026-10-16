#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace geoputer {

// Storage lives with the owner. Queue creation does not allocate another heap buffer.
template <typename Message, size_t Capacity>
class Queue {
public:
    static_assert(Capacity > 0, "Queue capacity must be positive");
    static_assert(std::is_trivially_copyable<Message>::value, "Messages must be value types");

    Queue() = default;
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    bool initialize() {
        if (!handle_) {
            handle_ = xQueueCreateStatic(Capacity, sizeof(Message), storage_, &control_);
        }
        return handle_ != nullptr;
    }

    bool available() const { return handle_ != nullptr; }
    bool send(const Message& message, TickType_t timeout = 0) {
        return handle_ && xQueueSend(handle_, &message, timeout) == pdTRUE;
    }
    bool receive(Message& message, TickType_t timeout = 0) {
        return handle_ && xQueueReceive(handle_, &message, timeout) == pdTRUE;
    }

private:
    StaticQueue_t control_{};
    alignas(Message) uint8_t storage_[sizeof(Message) * Capacity]{};
    QueueHandle_t handle_ = nullptr;
};

enum class SpeculativeResult { Queued, QueueFull, DestinationUnavailable };

template <typename Message, size_t Capacity>
SpeculativeResult sendSpeculative(Queue<Message, Capacity>& destination,
                                 const Message& message) {
    if (!destination.available()) return SpeculativeResult::DestinationUnavailable;
    return destination.send(message) ? SpeculativeResult::Queued : SpeculativeResult::QueueFull;
}

}  // namespace geoputer


#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "Queue.h"

namespace geoputer {

// Subtask base: one receive queue and one FreeRTOS task. Application runs on loopTask.
template <typename Message, size_t QueueCapacity>
class Task {
public:
    Task() = default;
    virtual ~Task() = default;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    bool initialize() { return inbox_.initialize(); }
    Queue<Message, QueueCapacity>& inbox() { return inbox_; }

    bool start(const char* name, uint32_t stack_bytes, UBaseType_t priority) {
        if (handle_ || !inbox_.available()) return false;
        return xTaskCreate(&Task::entry, name, stack_bytes, this, priority, &handle_) == pdPASS;
    }

protected:
    virtual void run() = 0;

private:
    static void entry(void* owner) {
        static_cast<Task*>(owner)->run();
        // The task objects have application lifetime. A task loop must not return.
        configASSERT(false);
        vTaskDelete(nullptr);
    }

    Queue<Message, QueueCapacity> inbox_;
    TaskHandle_t handle_ = nullptr;
};

}  // namespace geoputer


#pragma once
#include <freertos/FreeRTOS.h>
#include <cstring>

inline QueueHandle_t xQueueCreateStatic(size_t capacity, size_t item_size,
                                       uint8_t* storage, StaticQueue_t* control) {
    *control = {capacity, item_size, 0, 0, 0, storage};
    return control;
}

inline void (*hostQueueHook)(QueueHandle_t)=nullptr;

inline BaseType_t xQueueSend(QueueHandle_t queue, const void* value, TickType_t) {
    if (queue->count == queue->capacity) return pdFALSE;
    std::memcpy(queue->storage + queue->tail * queue->item_size, value, queue->item_size);
    queue->tail = (queue->tail + 1) % queue->capacity;
    ++queue->count;
    if(hostQueueHook)hostQueueHook(queue);
    return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void* value, TickType_t) {
    if (!queue->count) return pdFALSE;
    std::memcpy(value, queue->storage + queue->head * queue->item_size, queue->item_size);
    queue->head = (queue->head + 1) % queue->capacity;
    --queue->count;
    return pdTRUE;
}


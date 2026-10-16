#pragma once

// Host-only queue adapter. Firmware includes the ESP32 FreeRTOS headers instead.
#include <cstddef>
#include <cstdint>

using TickType_t = uint32_t;
using UBaseType_t = unsigned;
using BaseType_t = int;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr TickType_t portTICK_PERIOD_MS = 1;

struct StaticQueue_t {
    size_t capacity, item_size, head, tail, count;
    uint8_t* storage;
};
using QueueHandle_t = StaticQueue_t*;


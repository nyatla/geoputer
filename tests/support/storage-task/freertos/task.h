#pragma once
#include <freertos/FreeRTOS.h>
#include <cassert>
using TaskHandle_t = void*;
constexpr BaseType_t pdPASS = pdTRUE;
inline TickType_t xTaskGetTickCount() { return 123; }
inline BaseType_t xTaskCreate(void (*)(void*),const char*,uint32_t,void*,UBaseType_t,TaskHandle_t*) { return pdFALSE; }
inline void vTaskDelete(TaskHandle_t) {}
#define configASSERT(value) assert(value)

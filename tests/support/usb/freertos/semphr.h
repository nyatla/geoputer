#pragma once
#include <mutex>
#include <freertos/FreeRTOS.h>
using SemaphoreHandle_t=std::mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() {return new std::mutex;}
inline void xSemaphoreTake(SemaphoreHandle_t mutex,TickType_t) {mutex->lock();}
inline void xSemaphoreGive(SemaphoreHandle_t mutex) {mutex->unlock();}
inline void vTaskDelay(TickType_t) {}
inline TickType_t pdMS_TO_TICKS(unsigned ms) {return ms;}

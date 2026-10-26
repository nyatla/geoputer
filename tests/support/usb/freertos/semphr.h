#pragma once
#include <mutex>
#include <freertos/FreeRTOS.h>
using SemaphoreHandle_t=std::mutex*;
using StaticSemaphore_t=std::mutex;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage) {return storage;}
inline void xSemaphoreTake(SemaphoreHandle_t mutex,TickType_t) {mutex->lock();}
inline void xSemaphoreGive(SemaphoreHandle_t mutex) {mutex->unlock();}
inline void vTaskDelay(TickType_t) {}
inline TickType_t pdMS_TO_TICKS(unsigned ms) {return ms;}

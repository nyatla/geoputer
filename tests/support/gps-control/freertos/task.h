#pragma once
#include <freertos/FreeRTOS.h>
#include <cassert>
#include "../../usb/freertos/semphr.h"
using TaskHandle_t=void*;
constexpr BaseType_t pdPASS=pdTRUE;
inline TickType_t host_tick=0;
inline TickType_t xTaskGetTickCount(){return host_tick;}
inline BaseType_t xTaskCreate(void(*)(void*),const char*,uint32_t,void*,UBaseType_t,TaskHandle_t*){return pdFALSE;}
inline void vTaskDelete(TaskHandle_t){}
inline void vTaskDelayUntil(TickType_t*,TickType_t){}
#define configASSERT(value) assert(value)


inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t){return 8192;}

#pragma once
#include "../Config.h"
#include <cstddef>
#include <cstdint>

#if defined(ARDUINO_ARCH_ESP32)
#include "Console.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace geoputer {
// Point samples of global heap state. min_boot is the allocator's lifetime
// low-water mark, not a minimum reset for each map request. No owned buffers.
inline void reportMapHeap(const char* stage, uint32_t token, size_t bytes = 0) {
#if defined(ARDUINO_ARCH_ESP32)
    if (!config::MapHeapDiagnostics) return;
    const auto free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const auto minimum = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    const auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const auto internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const auto internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const auto stack = uxTaskGetStackHighWaterMark(nullptr);
    Console.printf("[MAP-HEAP] stage=%s token=%lu heap=%u min_boot=%u largest=%u internal=%u internal_largest=%u stack_free=%u bytes=%u\n",
        stage, static_cast<unsigned long>(token), static_cast<unsigned>(free), static_cast<unsigned>(minimum),
        static_cast<unsigned>(largest), static_cast<unsigned>(internal_free), static_cast<unsigned>(internal_largest),
        static_cast<unsigned>(stack), static_cast<unsigned>(bytes));
#else
    (void)stage; (void)token; (void)bytes;
#endif
}
} // namespace geoputer

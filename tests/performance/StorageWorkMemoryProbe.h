#pragma once
#include <atomic>
#include <cstring>
#include <climits>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../utils/Console.h"

namespace geoputer::benchmark {
enum Phase : unsigned {Off,Idle,MapPrepare,MapRead,Boundary,Publish,Place,LogRead,Count};
struct Stat {uint32_t free=UINT32_MAX,largest=UINT32_MAX,samples=0;};
inline Stat stats[Count];
inline std::atomic<unsigned> phase{Off};
inline portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
inline std::atomic<bool> started{false};
inline void sample() {
    const auto p=phase.load();if(!p)return;
    const uint32_t free=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    portENTER_CRITICAL(&lock);
    if(free<stats[p].free)stats[p].free=free;
    if(largest<stats[p].largest)stats[p].largest=largest;
    ++stats[p].samples;
    portEXIT_CRITICAL(&lock);
}
inline void mark(const char* name) {
    if(!started)return;
    if(!std::strcmp(name,"read-begin"))phase=MapRead;
    else if(!std::strcmp(name,"map-request") || !std::strcmp(name,"ui-begin"))phase=MapPrepare;
    else if(!std::strcmp(name,"source-ready") || !std::strcmp(name,"render-begin"))phase=Boundary;
    else if(!std::strcmp(name,"render-done-retained"))phase=Publish;
    else if(!std::strcmp(name,"place-begin-retained"))phase=Place;
    else if(!std::strcmp(name,"log-read-begin"))phase=LogRead;
    else if(!std::strcmp(name,"released-retained") || !std::strcmp(name,"place-done-retained") ||
            !std::strcmp(name,"session-closed") || !std::strcmp(name,"log-read-done"))phase=Idle;
    sample();
}
inline bool begin() {
    if(started)return true;
    const auto ok=xTaskCreate([](void*) {for(;;){sample();vTaskDelay(pdMS_TO_TICKS(1));}},"HeapProbe",2048,nullptr,3,nullptr)==pdPASS;
    started=ok;if(ok)phase=Idle;return ok;
}
inline void dump() {
    sample();started=false;phase=Off;
    const char* names[]={"off","idle","map-prepare","map-read","boundary","publish","place","log-read"};
    for(unsigned i=1;i<Count;++i) {
        Stat value;
        portENTER_CRITICAL(&lock);value=stats[i];portEXIT_CRITICAL(&lock);
        Console.printf("[WORK-MEMORY] phase=%s free=%lu largest=%lu samples=%lu\n",names[i],
            static_cast<unsigned long>(value.free),static_cast<unsigned long>(value.largest),static_cast<unsigned long>(value.samples));
    }
    Console.printf("[WORK-MEMORY] min_boot=%u\n",unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)));
}
} // namespace geoputer::benchmark

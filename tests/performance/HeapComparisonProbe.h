#pragma once
#include <atomic>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../utils/Console.h"
namespace geoputer::benchmark {
constexpr uint32_t Caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
enum Phase : unsigned {Off,Boot,Live,Map1Build,Map1,Map4Build,Map4,LogMapBuild,LogMap,Text,List,FileOpen,Properties,Point,Statistics,StatsResult,Return,LiveEnd,Count};
struct Stat {uint32_t total=0,min_free=UINT32_MAX,min_largest=UINT32_MAX,max_used=0,max_blocks=0,samples=0;};
inline Stat stats[Count];
inline std::atomic<unsigned> phase{Off};
inline portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
inline const char* name(unsigned p) {
    const char* names[]={"off","boot","live","map1-build","map1-hold","map4-build","map4-hold","log-map-build","log-map-hold","log-text","list","file-open","properties","point","statistics","stats-result","return","live-end"};
    return p<Count?names[p]:"invalid";
}
inline void sample() {
    const auto p=phase.load();if(!p)return;
    const uint32_t total=heap_caps_get_total_size(Caps);
    multi_heap_info_t info{};heap_caps_get_info(&info,Caps);
    portENTER_CRITICAL(&lock);
    auto& s=stats[p];s.total=total;
    if(info.total_free_bytes<s.min_free)s.min_free=info.total_free_bytes;
    if(info.largest_free_block<s.min_largest)s.min_largest=info.largest_free_block;
    if(total-info.total_free_bytes>s.max_used)s.max_used=total-info.total_free_bytes;
    if(info.allocated_blocks>s.max_blocks)s.max_blocks=info.allocated_blocks;
    ++s.samples;
    portEXIT_CRITICAL(&lock);
}
inline void mark(Phase p) {phase=p;sample();}
inline bool begin() {
    mark(Boot);
    return xTaskCreate([](void*){for(;;){sample();vTaskDelay(1);}},"HeapCompare",2048,nullptr,3,nullptr)==pdPASS;
}
inline void snapshot(unsigned cycle,const char* label) {
    sample();multi_heap_info_t info{};heap_caps_get_info(&info,Caps);
    const auto total=heap_caps_get_total_size(Caps);
    Console.printf("[HEAP-STATE] cycle=%u label=%s total=%u free=%u used=%u allocated=%u largest=%u blocks=%u min_boot=%u app_stack=%u\n",cycle,label,
        unsigned(total),unsigned(info.total_free_bytes),unsigned(total-info.total_free_bytes),unsigned(info.total_allocated_bytes),
        unsigned(info.largest_free_block),unsigned(info.allocated_blocks),unsigned(info.minimum_free_bytes),unsigned(uxTaskGetStackHighWaterMark(nullptr)));
}
inline void dump() {
    sample();phase=Off;
    for(unsigned i=1;i<Count;++i) {
        Stat value;portENTER_CRITICAL(&lock);value=stats[i];portEXIT_CRITICAL(&lock);
        if(value.samples)Console.printf("[HEAP-PHASE] label=%s total=%lu min_free=%lu max_used=%lu min_largest=%lu max_blocks=%lu samples=%lu\n",name(i),
            (unsigned long)value.total,(unsigned long)value.min_free,(unsigned long)value.max_used,(unsigned long)value.min_largest,
            (unsigned long)value.max_blocks,(unsigned long)value.samples);
    }
}
} // namespace geoputer::benchmark

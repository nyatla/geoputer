#pragma once
#include "BeforeMapBuildJob.h"
#include "task/storage/MapBuildJob.h"
#include "map/BoundaryImageView.h"
#include "utils/ElapsedTime.h"
#include <esp_heap_caps.h>
#include <sys/stat.h>
#include <stdexcept>
namespace geoputer::benchmark {
union JobStorage {BeforeMapBuildJob before;MapBuildJob after;JobStorage(){}~JobStorage(){}};
struct Result {uint64_t total,read,render,hash;uint32_t before,minimum,after,stack;};
inline uint32_t heap(){return heap_caps_get_free_size(MALLOC_CAP_8BIT);}
template<class Job> Result measure(JobStorage& storage,map::FixedBoundaryImage& output,
                                    map::CodeImageBuffer& pixels,const SelectedDataset& selected,
                                    uint8_t drive,unsigned scale,unsigned site,unsigned token) {
    map::Request request{};request.token=token;request.screen_generation=1;request.dataset_generation=selected.info.generation;
    request.latitude=site?35.681641:35.681641;request.longitude=site?140.027194:140.027194;request.scale=scale;
    request.buffer={output.storageData(),output.Capacity,token};
    map::MapSession session(pixels);
    auto* job=::new(static_cast<void*>(&storage)) Job;
    struct End {Job* job;~End(){job->~Job();}} end{job};
    Result result{};result.before=result.minimum=heap();
    const auto start=monotonicMicros();
    if(!job->start(request,session))throw std::runtime_error("map start");
    const auto read_start=monotonicMicros();
    auto source=readBoundaryMap(selected,request,drive,nullptr,std::move(session),job->readBuffer(),job->readFile(),job->pathBuffer());
    result.read=monotonicMicros()-read_start;result.minimum=std::min(result.minimum,heap());
    const auto render_start=monotonicMicros();job->loaded(std::move(source));
    while(!job->complete())job->step();
    result.render=monotonicMicros()-render_start;result.minimum=std::min(result.minimum,heap());
    const auto error=job->info().error;
    if(!job->release(request) || error!=map::Error::None)throw std::runtime_error("map read/render/release");
    result.total=monotonicMicros()-start;result.after=heap();result.minimum=std::min(result.minimum,result.after);
    result.stack=uxTaskGetStackHighWaterMark(nullptr);
    uint64_t hash=14695981039346656037ull;
    for(size_t n=0;n<map::BoundaryBytes;++n){hash^=output.storageData()[n];hash*=1099511628211ull;}
    result.hash=hash;return result;
}
template<class Print> unsigned runBorrowLayout(const SelectedDataset& selected,map::CodeImageBuffer& pixels,uint8_t drive,unsigned number,Print print) {
    static JobStorage jobs;static map::FixedBoundaryImage output;
    unsigned cases=0;uint64_t hashes[2][3]{};
    for(unsigned site=0;site<2;++site)for(unsigned round=0;round<4;++round)for(unsigned index=0;index<3;++index)for(unsigned order=0;order<2;++order) {
        const bool after=(order^(round&1))!=0;const unsigned scale=1u<<index;
        const auto result=after?measure<MapBuildJob>(jobs,output,pixels,selected,drive,scale,site,++cases)
                               :measure<BeforeMapBuildJob>(jobs,output,pixels,selected,drive,scale,site,++cases);
        if(hashes[site][index] && hashes[site][index]!=result.hash)throw std::runtime_error("boundary hash changed");
        hashes[site][index]=result.hash;
        print("[BORROW-LAYOUT] dataset=%02u site=%u round=%u scale=%u mode=%s total_us=%llu read_us=%llu render_us=%llu heap_before=%lu heap_min=%lu heap_after=%lu stack=%lu hash=%llu\n",
            number,site,round,scale,after?"after":"before",(unsigned long long)result.total,(unsigned long long)result.read,(unsigned long long)result.render,
            (unsigned long)result.before,(unsigned long)result.minimum,(unsigned long)result.after,(unsigned long)result.stack,(unsigned long long)result.hash);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return cases;
}
} // namespace geoputer::benchmark

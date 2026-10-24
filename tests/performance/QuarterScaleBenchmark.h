#pragma once
#include "ScaledBoundaryPrototype.h"
#include "task/storage/galuchat/DiskIndexedMapReader.h"
#include "task/storage/galuchat/ObservedFileReader.h"
#include "task/storage/dataset/DatasetMetadata.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
namespace geoputer::benchmark {
class ScaleHeapObserver final:public StorageObserver {
public:
    explicit ScaleHeapObserver(StorageObserver* parent):parent_(parent){reset();}
    void reset(){minimum=SIZE_MAX;largest_at_min=0;sample();}
    void sample(){const auto free=heap_caps_get_free_size(MALLOC_CAP_8BIT);if(free<minimum){minimum=free;largest_at_min=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);}}
    void sdBegin() override {sample();if(parent_)parent_->sdBegin();}
    void sdEnd() override {sample();if(parent_)parent_->sdEnd();}
    void cacheState(RgCacheState,bool=false) override {}
    size_t minimum=0,largest_at_min=0;
private:StorageObserver* parent_;
};
template<class Print> void runQuarterScaleBenchmark(const std::string& directory,const char* language,uint8_t drive,
                                                    double latitude,double longitude,StorageObserver* parent,Print print) {
    const auto metadata=readDatasetMetadata(directory,UiLanguage::Japanese,language,parent);
    if(metadata.error!=DatasetError::None)throw std::runtime_error("quarter benchmark metadata");
    const auto map_path=directory+"/"+metadata.map,index_path=directory+"/_mapindex.dat";
    const auto info=DatasetIndex(drive,parent).loadMap(index_path,map_path);
    ScaleHeapObserver observer(parent);FileIoTiming map_io,index_io;
    auto map_factory=std::make_shared<ObservedFileReaderFactory>(map_path,&observer,&map_io,FileReadOptions{16384,4096},drive);
    auto index_factory=std::make_shared<ObservedFileReaderFactory>(index_path,&observer,&index_io,FileReadOptions{4096,0},drive);
    DiskIndexedMapReader reader(info,map_factory,index_factory);
    const auto normal=scalePlan(latitude,longitude,reader.unitInvX(),reader.unitInvY(),1),half=scalePlan(latitude,longitude,reader.unitInvX(),reader.unitInvY(),2),quarter=scalePlan(latitude,longitude,reader.unitInvX(),reader.unitInvY(),4);
    // Reserve the same two UI image buffers for all cases. No UI-frame copying
    // or Wordbook work is timed; existing point/name caches stay untouched.
    map::BoundaryImage front,back; if(!front.resize(240,101)||!back.resize(240,101))throw std::bad_alloc();
    print("[QUARTER-BEGIN] lat=%.6f lon=%.6f path=%s chunks=%u full=%dx%d half=%dx%d quarter=%dx%d ui_reserved=12120 map_buffer=16384 index_buffer=4096 spi_hz=%lu\n",
        latitude,longitude,map_path.c_str(),reader.chunkCount(),normal.source.width,normal.source.height,half.source.width,half.source.height,quarter.source.width,quarter.source.height,static_cast<unsigned long>(config::SdSpiHz));
    uint64_t hashes[3]{};bool have[3]{};uint32_t current=0;
    const unsigned order[3][5]={{0,1,3,2,4},{4,2,0,3,1},{3,1,4,0,2}};
    for(unsigned round=0;round<3;++round)for(unsigned kind:order[round]) {
        const unsigned scale=kind==0?1:kind<=2?2:4,band=kind==0?0:(kind%2?8:16);
        map_io={};index_io={};observer.reset();
        const auto before=heap_caps_get_free_size(MALLOC_CAP_8BIT),largest_before=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        const auto started=monotonicMicros();uint64_t read_us=0,extract_us=0,yield_us=0,hash=0;size_t work_bytes=0;unsigned calls=0,white=0,gray=0;
        {
            map::BoundaryImage out;
            if(kind==0) {
                work_bytes=size_t(normal.source.width)*normal.source.height*4;std::unique_ptr<uint32_t[]> pixels(new uint32_t[work_bytes/4]);observer.sample();
                auto stage=monotonicMicros();reader.readRect({normal.source.x,normal.source.y,normal.source.width,normal.source.height},pixels.get());read_us=monotonicMicros()-stage;calls=1;observer.sample();
                const map::RasterView view{pixels.get(),normal.source,work_bytes/4};current=view.get(normal.base.center_x,normal.base.center_y);
                if(!out.resize(240,101))throw std::bad_alloc();
                stage=monotonicMicros();if(map::buildBoundaryRows(view,normal.base,current,0,101,out)!=map::Error::None)throw std::runtime_error("quarter baseline conversion");extract_us=monotonicMicros()-stage;
            } else {
                const auto stats=renderStrips(scale==2?half:quarter,current,band,out,[&](const galuchat::Rect& r,uint32_t* dst){observer.sample();reader.readRect(r,dst);observer.sample();},[&]{observer.sample();vTaskDelay(1);});
                read_us=stats.read_us;extract_us=stats.extract_us;yield_us=stats.yield_us;work_bytes=stats.work_bytes;calls=stats.calls;
                const auto& plan=scale==2?half:quarter;
                if(stats.source_pixels!=uint64_t(plan.source.width)*plan.source.height)throw std::runtime_error("quarter requested duplicate raw pixels");
            }
            observer.sample();hash=boundaryHash(out);
            for(unsigned y=0;y<101;++y)for(unsigned x=0;x<240;++x){const auto v=out.get(x,y);white+=v==2;gray+=v==1;}
        }
        const auto elapsed=monotonicMicros()-started;const auto after=heap_caps_get_free_size(MALLOC_CAP_8BIT);const unsigned hash_index=kind==0?0:scale==2?1:2;
        if(have[hash_index]&&hashes[hash_index]!=hash)throw std::runtime_error("quarter stripe/round image mismatch");
        hashes[hash_index]=hash;have[hash_index]=true;
        print("[QUARTER-CASE] round=%u scale=%u band=%u total_us=%llu read_us=%llu extract_us=%llu yield_us=%llu calls=%u work=%u hash=%llu white=%u gray=%u heap_before=%u heap_after=%u heap_sample_min=%u largest_at_sample_min=%u largest_before=%u min_boot=%u stack=%u\n",
            round+1,scale,band,(unsigned long long)elapsed,(unsigned long long)read_us,(unsigned long long)extract_us,(unsigned long long)yield_us,calls,(unsigned)work_bytes,(unsigned long long)hash,white,gray,(unsigned)before,(unsigned)after,(unsigned)observer.minimum,(unsigned)observer.largest_at_min,(unsigned)largest_before,(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),(unsigned)uxTaskGetStackHighWaterMark(nullptr));
        auto io=[&](const char* file,const FileIoTiming& t){print("[QUARTER-IO] round=%u scale=%u band=%u file=%s bytes=%llu reads=%u seeks=%u opens=%u closes=%u read_us=%llu seek_us=%llu open_us=%llu close_us=%llu\n",round+1,scale,band,file,(unsigned long long)t.bytes,t.reads,t.seeks,t.opens,t.closes,(unsigned long long)t.read_us,(unsigned long long)t.seek_us,(unsigned long long)t.open_us,(unsigned long long)t.close_us);};
        io("map",map_io);io("index",index_io);vTaskDelay(pdMS_TO_TICKS(100));
    }
    print("[QUARTER-DONE] cases=15 half_equal=1 quarter_equal=1 point_cache_untouched=1\n");
}
} // namespace geoputer::benchmark

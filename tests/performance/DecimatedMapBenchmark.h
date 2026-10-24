#pragma once
#include "QuarterScaleBenchmark.h"
#include "DecodeProfile.h"
#include "map/DecimatedViewport.h"
#include <atomic>
namespace geoputer::benchmark {
inline std::atomic<uint32_t> decimation_background_ms{0};
template<class Print> void runDecimatedMapBenchmark(const std::string& directory,const char* language,
                                                    uint8_t drive,StorageObserver* parent,Print print) {
    constexpr double latitude=35.681641,longitude=140.027194;
    const auto metadata=readDatasetMetadata(directory,UiLanguage::Japanese,language,parent);
    if(metadata.error!=DatasetError::None)throw std::runtime_error("decimation metadata");
    const auto path=directory+"/"+metadata.map,index_path=directory+"/_mapindex.dat";
    const auto info=DatasetIndex(drive,parent).loadMap(index_path,path);
    ScaleHeapObserver observer(parent);FileIoTiming map_io,index_io;
    DiskIndexedMapReader reader(info,
        std::make_shared<ObservedFileReaderFactory>(path,&observer,&map_io,FileReadOptions{config::MapRectReadBufferBytes,config::FileReadThroughSkipBytes},drive),
        std::make_shared<ObservedFileReaderFactory>(index_path,&observer,&index_io,FileReadOptions{config::IndexReadBufferBytes,0},drive));
    map::BoundaryImage front,back;
    if(!front.resize(240,101)||!back.resize(240,101))throw std::bad_alloc();
    print("[DECIM-BEGIN] lat=%.6f lon=%.6f path=%s chunks=%u rounds=6 ui_reserved=12120 point_cache_retained=1\n",
          latitude,longitude,path.c_str(),reader.chunkCount());
    uint32_t current=0;
    for(unsigned scale:{1u,2u,4u}) {
        map::DecimatedViewport sampled;
        if(map::planDecimatedViewport(latitude,longitude,reader.unitInvX(),reader.unitInvY(),config::MapViewCodeMaxBytes,scale,sampled)!=map::Error::None)
            throw std::runtime_error("decimated viewport");
        const auto legacy=scalePlan(latitude,longitude,reader.unitInvX(),reader.unitInvY(),scale);
        const unsigned modes=scale==1?2:3;
        uint64_t expected=0,old_expected=0;
        print("[DECIM-PLAN] scale=%u input=%dx%d stored=%dx%d old_input=%dx%d work=%u mpp=%.6f\n",
              scale,sampled.input.width,sampled.input.height,sampled.output.source.width,sampled.output.source.height,
              legacy.source.width,legacy.source.height,unsigned(size_t(sampled.output.source.width)*sampled.output.source.height*4),sampled.output.meters_per_pixel);
        for(unsigned round=0;round<6;++round)for(unsigned slot=0;slot<modes;++slot) {
            const unsigned mode=(slot+round)%modes;
            const char* name=mode==0?"single":mode==1?"strips16":"old_edges16";
            print("[DECIM-START] scale=%u mode=%s round=%u heap=%u largest=%u\n",scale,name,round+1,
                  unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
            map_io={};index_io={};observer.reset();DecodeProfile decode{&map_io,&index_io};ActivateDecodeProfile active(decode);
            map::BoundaryImage out;uint64_t read_us=0,extract_us=0,yield_us=0;unsigned calls=0;size_t work=0;
            const auto start=monotonicMicros();
            if(mode==2) {
                const auto stats=renderStrips(legacy,current,16,out,
                    [&](const galuchat::Rect& rect,uint32_t* dst){observer.sample();reader.readRect(rect,dst);observer.sample();},
                    [&]{observer.sample();vTaskDelay(1);});
                read_us=stats.read_us;extract_us=stats.extract_us;yield_us=stats.yield_us;calls=stats.calls;work=stats.work_bytes;
            } else {
                const auto& bounds=sampled.output.source;const auto& raw=sampled.input;
                work=size_t(bounds.width)*bounds.height*4;
                std::unique_ptr<uint32_t[]> pixels(new uint32_t[work/4]);observer.sample();
                const auto tick=monotonicMicros();
                if(mode==0) {
                    reader.readRectDecimated({raw.x,raw.y,raw.width,raw.height},scale,pixels.get());calls=1;
                } else {
                    // Same sampled image and source lattice, only batching differs.
                    for(int offset=0;offset<raw.height;offset+=16) {
                        const int rows=std::min(16,raw.height-offset);
                        reader.readRectDecimated({raw.x,raw.y+offset,raw.width,rows},scale,
                            pixels.get()+size_t(unsigned(offset)/scale)*bounds.width);
                        ++calls;
                    }
                }
                read_us=monotonicMicros()-tick;observer.sample();
                const map::RasterView view{pixels.get(),bounds,work/4};
                const auto region=view.get(sampled.output.center_x,sampled.output.center_y);
                if(scale==1 && round==0 && slot==0)current=region;
                if(region!=current)throw std::runtime_error("decimated current-location sample changed");
                if(!out.resize(240,101))throw std::bad_alloc();
                const auto edge_start=monotonicMicros();
                for(unsigned row=0;row<101;row+=config::MapViewRowsPerStep)
                    if(map::buildBoundaryRows(view,sampled.output,current,row,std::min(config::MapViewRowsPerStep,101-row),out)!=map::Error::None)
                        throw std::runtime_error("decimated edge extraction");
                extract_us=monotonicMicros()-edge_start;observer.sample();
            }
            const auto elapsed=monotonicMicros()-start;
            const auto hash=boundaryHash(out);auto& reference=mode==2?old_expected:expected;
            if(reference && reference!=hash)throw std::runtime_error("decimated images differ across batching or rounds");
            reference=hash;
            const auto file_us=map_io.elapsed_us+index_io.elapsed_us;
            if(decode.io_us>decode.inclusive_us || file_us+decode.inclusive_us-decode.io_us>read_us)
                throw std::runtime_error("decimation timing overlap");
            const auto decode_us=decode.inclusive_us-decode.io_us;
            print("[DECIM-CASE] scale=%u mode=%s round=%u total_us=%llu read_us=%llu file_us=%llu decode_us=%llu index_other_us=%llu extract_us=%llu yield_us=%llu calls=%u decode_calls=%u work=%u current=%u hash=%llu heap_min=%u largest_at_min=%u\n",
                  scale,name,round+1,(unsigned long long)elapsed,(unsigned long long)read_us,(unsigned long long)file_us,
                  (unsigned long long)decode_us,(unsigned long long)(read_us-file_us-decode_us),(unsigned long long)extract_us,
                  (unsigned long long)yield_us,calls,decode.calls,unsigned(work),current,(unsigned long long)hash,unsigned(observer.minimum),unsigned(observer.largest_at_min));
            auto io=[&](const char* file,const FileIoTiming& t){
                print("[DECIM-IO] scale=%u mode=%s round=%u file=%s bytes=%llu reads=%u seeks=%u opens=%u closes=%u read_us=%llu seek_us=%llu open_us=%llu close_us=%llu\n",
                      scale,name,round+1,file,(unsigned long long)t.bytes,t.reads,t.seeks,t.opens,t.closes,
                      (unsigned long long)t.read_us,(unsigned long long)t.seek_us,(unsigned long long)t.open_us,(unsigned long long)t.close_us);
            };io("map",map_io);io("index",index_io);vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    print("[DECIM-DONE] cases=48 sampled_images_equal=1 center_equal=1\n");
}
} // namespace geoputer::benchmark

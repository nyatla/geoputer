#pragma once
#include "task/storage/galuchat/CachedMapReader.h"
#include "task/storage/galuchat/ObservedFileReader.h"
#include "task/storage/dataset/DatasetMetadata.h"
#include "map/BoundaryImage.h"
#include <Arduino.h>
#include <dirent.h>
#include <vector>
#include <cmath>

namespace geoputer::benchmark {
// Diagnostic only: read real files, reserve the two map UI images, and compare
// cache sides in alternating order. Does not write datasets, indices or NVS.
template<class Print> void runCodeCacheBenchmark(uint8_t drive,StorageObserver* observer,Print print) {
    map::BoundaryImage front,back;
    if(!front.resize(240,101)||!back.resize(240,101))throw std::bad_alloc();
    std::vector<std::string> directories;
    DIR* directory=opendir("/sd/dataset");if(!directory)throw std::runtime_error("dataset directory");
    while(const auto* entry=readdir(directory)) {
        const std::string name=entry->d_name;
        if(name.size()==2 && name[0]>='0' && name[0]<='9' && name[1]>='0' && name[1]<='9')
            directories.push_back("/sd/dataset/"+name);
    }
    closedir(directory);std::sort(directories.begin(),directories.end());
    unsigned cases=0,datasets=0;
    print("[CODE-CACHE-BEGIN] sizes=128,64,32 rounds=3 sites=2 ui_reserved=12120 sd_requested=%lu\n",static_cast<unsigned long>(config::SdSpiHz));
    for(const auto& dir:directories) {
        try {
            const auto metadata=readDatasetMetadata(dir,UiLanguage::Japanese,"",observer);
            if(metadata.error!=DatasetError::None)throw std::runtime_error("metadata");
            const auto path=dir+"/"+metadata.map,index_path=dir+"/_mapindex.dat";
            const auto info=DatasetIndex(drive,observer).loadMap(index_path,path);
            FileIoTiming map_io,index_io;
            auto map_factory=std::make_shared<ObservedFileReaderFactory>(path,observer,&map_io,
                FileReadOptions{config::MapRectReadBufferBytes,config::FileReadThroughSkipBytes},drive);
            auto index_factory=std::make_shared<ObservedFileReaderFactory>(index_path,observer,&index_io,
                FileReadOptions{config::IndexReadBufferBytes,0},drive);
            DiskIndexedMapReader reference(info,map_factory,index_factory);
            const int ux=reference.unitInvX(),uy=reference.unitInvY();
            print("[CODE-CACHE-DATASET] dir=%s map=%s chunks=%u unit=%d,%d\n",dir.c_str(),metadata.map.c_str(),reference.chunkCount(),ux,uy);
            ++datasets;
            const double sites[][2]={{35.681641,140.027194},{35.681641,140.027194}};
            const unsigned order[][3]={{128,64,32},{32,128,64},{64,32,128}};
            for(unsigned site=0;site<2;++site) {
                const double lat=sites[site][0],lon=sites[site][1];
                const int x=std::lround(lon*ux),y=std::lround(lat*uy);
                uint32_t expected=0;reference.readRect({x,y,1,1},&expected);
                for(unsigned round=0;round<3;++round)for(unsigned side:order[round]) {
                    CodeCacheBuffer pixels{};CachedMapReader cache(pixels,info,map_factory,index_factory,observer,side);
                    const auto heap_before=ESP.getFreeHeap();
                    map_io={};index_io={};
                    const auto start=monotonicMicros();const auto code=cache.readWgsPoint(lon,lat);const auto elapsed=monotonicMicros()-start;
                    if(code!=expected)throw std::runtime_error("cache center differs from point reference");
                    const auto heap_after=ESP.getFreeHeap();
                    print("[CODE-CACHE-CASE] dir=%s site=%u side=%u round=%u total_us=%llu rect_us=%llu code=%lu bytes=%u map_bytes=%llu map_reads=%lu map_seeks=%lu index_bytes=%llu index_reads=%lu index_seeks=%lu heap_before=%u heap_after=%u\n",
                        dir.c_str(),site,side,round+1,(unsigned long long)elapsed,(unsigned long long)cache.stats().last_rect_us,
                        (unsigned long)code,unsigned(cache.stats().bytes),(unsigned long long)map_io.bytes,(unsigned long)map_io.reads,(unsigned long)map_io.seeks,
                        (unsigned long long)index_io.bytes,(unsigned long)index_io.reads,(unsigned long)index_io.seeks,heap_before,heap_after);
                    const auto fills=cache.stats().fills;
                    map_io={};index_io={};const auto hit_start=monotonicMicros();
                    for(unsigned n=0;n<100;++n)cache.readWgsPoint(double(x+1)/ux,double(y)/uy);
                    const auto hit_us=monotonicMicros()-hit_start;
                    if(cache.stats().fills!=fills || map_io.bytes || index_io.bytes || map_io.opens || index_io.opens)
                        throw std::runtime_error("cache hits accessed SD");
                    map_io={};index_io={};const auto crossing_start=monotonicMicros();
                    cache.readWgsPoint(double(x+int(side)/2)/ux,double(y)/uy);
                    const auto crossing_us=monotonicMicros()-crossing_start;
                    if(cache.stats().fills!=fills+1)throw std::runtime_error("crossing cache boundary did not refill");
                    print("[CODE-CACHE-FOLLOW] dir=%s site=%u side=%u round=%u hit100_us=%llu hit_io=0 crossing_us=%llu crossing_rect_us=%llu crossing_map_bytes=%llu crossing_index_bytes=%llu\n",
                        dir.c_str(),site,side,round+1,(unsigned long long)hit_us,(unsigned long long)crossing_us,
                        (unsigned long long)cache.stats().last_rect_us,(unsigned long long)map_io.bytes,(unsigned long long)index_io.bytes);
                    ++cases;vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
        }catch(const std::exception& error){print("[CODE-CACHE-SKIP] dir=%s reason=%s\n",dir.c_str(),error.what());}
    }
    print("[CODE-CACHE-DONE] datasets=%u cases=%u\n",datasets,cases);
}
}

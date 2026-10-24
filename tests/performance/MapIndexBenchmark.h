#pragma once
#include "baseline/RamCachedMapReader.h"
#include "task/storage/galuchat/CachedMapReader.h"
#include "task/storage/galuchat/ObservedFileReader.h"
#include <cstring>
#include <memory>
#include <stdexcept>
namespace geoputer::benchmark {
template<class Cache> uint64_t hashImage(Cache& cache) {
    const auto r=cache.stats().bounds;
    uint64_t hash=14695981039346656037ull;
    for(int y=0;y<128;++y) for(int x=0;x<128;++x) {
        hash^=cache.readWgsPoint(double(r.x+x)/cache.unitInvX(),double(r.y+y)/cache.unitInvY());hash*=1099511628211ull;
    }
    return hash;
}
template<class Print,class Heap>
void runMapIndexBenchmark(const std::string& directory,const DatasetMetadata& metadata,uint8_t drive,Print print,Heap heap) {
    const auto map_path=directory+"/"+metadata.map,index_path=directory+"/_mapindex.dat";
    DatasetIndex disk(drive);
    try { (void)disk.loadMap(index_path,map_path); }
    catch(const std::exception&) {
        const auto start=monotonicMicros();
        print("[DISK-INDEX-GENERATE] begin path=%s\n",index_path.c_str());
        const auto error=disk.apply(directory,metadata,[](void*){return true;},nullptr);
        if(error!=DatasetError::None) throw std::runtime_error("benchmark index generation failed");
        print("[DISK-INDEX-GENERATE] done elapsed_us=%llu\n",static_cast<unsigned long long>(monotonicMicros()-start));
    }
    print("[DISK-INDEX-BEGIN] path=%s map_buffer=16384 index_buffer=4096 skip_threshold=4096 observer=none\n",map_path.c_str());
    uint64_t expected[3]{};bool have_expected[3]{};
    const double centers[3][2]={{140.027194,35.681641},{141.4632,24.2349},{0,0}};
    for(unsigned round=0;round<3;++round) for(unsigned order=0;order<2;++order) {
        const bool saved=bool(order^(round%2));
        FileIoTiming map_io{},index_io{};
        const auto before_heap=heap();const auto start=monotonicMicros();
        auto factory=std::make_shared<ObservedFileReaderFactory>(map_path,nullptr,&map_io,FileReadOptions{4096,4096},drive);
        std::unique_ptr<RamCachedMapReader> ram;
        std::unique_ptr<CachedMapReader> file;
        if(saved) {
            const auto info=disk.loadMap(index_path,map_path,&map_io,&index_io);
            auto index=std::make_shared<ObservedFileReaderFactory>(index_path,nullptr,&index_io,FileReadOptions{4096,0},drive);
            file=std::make_unique<CachedMapReader>(info,factory,index);
        } else ram=std::make_unique<RamCachedMapReader>(factory);
        factory->setReadOptions({16384,4096});
        const auto init=monotonicMicros()-start;const auto after_heap=heap();
        const char* mode=saved?"disk":"ram";
        print("[DISK-INDEX-INIT] round=%u mode=%s init_us=%llu heap_before=%u heap_after=%u map_bytes=%llu index_bytes=%llu\n",round+1,mode,
            static_cast<unsigned long long>(init),before_heap,after_heap,static_cast<unsigned long long>(map_io.bytes),static_cast<unsigned long long>(index_io.bytes));
        auto run=[&](auto& cache) {
            for(unsigned scenario=0;scenario<3;++scenario) {
                cache.invalidate();map_io={};index_io={};
                const auto t=monotonicMicros();const auto code=cache.readWgsPoint(centers[scenario][0],centers[scenario][1]);
                const auto query=monotonicMicros()-t;
                const auto m=map_io,i=index_io;
                const auto hash=hashImage(cache);
                if(have_expected[scenario] && expected[scenario]!=hash)throw std::runtime_error("RAM/disk image mismatch");
                expected[scenario]=hash;have_expected[scenario]=true;
                map_io={};index_io={};const auto hit_start=monotonicMicros();
                if(cache.readWgsPoint(centers[scenario][0],centers[scenario][1])!=code)throw std::runtime_error("cache hit changed code");
                const auto hit=monotonicMicros()-hit_start;
                if(map_io.bytes || map_io.opens || index_io.bytes || index_io.opens)throw std::runtime_error("cache hit performed index/map IO");
                print("[DISK-INDEX-CASE] round=%u mode=%s scenario=%u query_us=%llu rect_us=%llu hit_us=%llu map_bytes=%llu map_reads=%u map_seeks=%u map_opens=%u map_read_us=%llu map_seek_us=%llu index_bytes=%llu index_reads=%u index_seeks=%u index_opens=%u index_read_us=%llu index_seek_us=%llu hash=%llu center=%u\n",
                    round+1,mode,scenario,static_cast<unsigned long long>(query),static_cast<unsigned long long>(cache.stats().last_rect_us),static_cast<unsigned long long>(hit),
                    static_cast<unsigned long long>(m.bytes),m.reads,m.seeks,m.opens,static_cast<unsigned long long>(m.read_us),static_cast<unsigned long long>(m.seek_us),
                    static_cast<unsigned long long>(i.bytes),i.reads,i.seeks,i.opens,static_cast<unsigned long long>(i.read_us),static_cast<unsigned long long>(i.seek_us),static_cast<unsigned long long>(hash),code);
            }
        };
        if(saved)run(*file);else run(*ram);
    }
    print("[DISK-INDEX-DONE] equal=1 cases=18\n");
}
}

#pragma once
#include "QuarterScaleBenchmark.h"
#include "BoundaryExtractorBefore.h"
#include "DecodeProfile.h"
#include <cstring>
#ifdef GEOPUTER_COMPARE_STDIO
#include "ObservedFileReaderStdio.h"
#endif
#ifdef GEOPUTER_COMPARE_RLE
#include "task/storage/galuchat/ScalarDiskIndexedMapReader.h"
#endif
#ifdef GEOPUTER_COMPARE_SPAN_FILL
#include "task/storage/galuchat/BeforeSpanMapReader.h"
#include "task/storage/galuchat/MonoSpanMapReader.h"
#include "task/storage/galuchat/ClipSpanMapReader.h"
#endif
#ifdef GEOPUTER_COMPARE_RAW
#include "RawCellBenchmark.h"
#include "task/storage/galuchat/RawBeforeMapReader.h"
#endif
namespace geoputer::benchmark {
template<class Print> void runMapPipelineBenchmark(const std::string& directory, const char* language,
                                                   uint8_t drive, StorageObserver* parent, Print print) {
#ifdef GEOPUTER_COMPARE_RAW
    runRawCellBenchmark(print,[]{vTaskDelay(1);});
#endif
    // Match the previous SPI-clock measurements rather than GPS jitter.
    constexpr double latitude = 35.681641, longitude = 140.027194;
    const auto metadata = readDatasetMetadata(directory, UiLanguage::Japanese, language, parent);
    if (metadata.error != DatasetError::None) throw std::runtime_error("pipeline metadata");
    const auto map_path = directory + "/" + metadata.map;
    const auto index_path = directory + "/_mapindex.dat";
    const auto info = DatasetIndex(drive, parent).loadMap(index_path, map_path);
    ScaleHeapObserver observer(parent);
    FileIoTiming map_io, index_io;
    auto map_factory = std::make_shared<ObservedFileReaderFactory>(map_path, &observer, &map_io,
        FileReadOptions{config::MapRectReadBufferBytes, config::FileReadThroughSkipBytes}, drive);
    auto index_factory = std::make_shared<ObservedFileReaderFactory>(index_path, &observer, &index_io,
        FileReadOptions{config::IndexReadBufferBytes, 0}, drive);
    DiskIndexedMapReader reader(info, map_factory, index_factory);
#ifdef GEOPUTER_COMPARE_RAW
    RawBeforeMapReader raw_before_reader(info,map_factory,index_factory);
#endif
    const auto normal = scalePlan(latitude,longitude,reader.unitInvX(),reader.unitInvY(),1);
    const auto half = scalePlan(latitude,longitude,reader.unitInvX(),reader.unitInvY(),2);
    const auto quarter = scalePlan(latitude,longitude,reader.unitInvX(),reader.unitInvY(),4);
    map::BoundaryImage front, back;
    if (!front.resize(240,101) || !back.resize(240,101)) throw std::bad_alloc();
    print("[PIPE-BEGIN] lat=%.6f lon=%.6f path=%s chunks=%u ui_reserved=12120 rows_per_step=%u\n",
        latitude,longitude,map_path.c_str(),reader.chunkCount(),config::MapViewRowsPerStep);
    uint64_t hashes[3]{};
    bool have_hash[3]{};
    uint32_t current = 0;
#ifdef GEOPUTER_COMPARE_SPAN_FILL
    BeforeSpanMapReader before_reader(info,map_factory,index_factory);
    MonoSpanMapReader mono_reader(info,map_factory,index_factory);
    ClipSpanMapReader clip_reader(info,map_factory,index_factory);
#endif
#ifdef GEOPUTER_COMPARE_RLE
    ScalarDiskIndexedMapReader scalar_reader(info, map_factory, index_factory);
#endif
#ifdef GEOPUTER_COMPARE_STDIO
    auto stdio_map = std::make_shared<StdioObservedFileReaderFactory>(map_path, &observer, &map_io,
        FileReadOptions{config::MapRectReadBufferBytes, config::FileReadThroughSkipBytes}, drive);
    auto stdio_index = std::make_shared<StdioObservedFileReaderFactory>(index_path, &observer, &index_io,
        FileReadOptions{config::IndexReadBufferBytes, 0}, drive);
    DiskIndexedMapReader stdio_reader(info, stdio_map, stdio_index);
    print("[PIPE-READER] fil_bytes=%u direct_reader_bytes=%u stdio_reader_bytes=%u\n",
        unsigned(sizeof(FIL)),unsigned(sizeof(ObservedFileReader)),unsigned(sizeof(StdioObservedFileReader)));
#endif
#if defined(GEOPUTER_COMPARE_SPAN_FILL)
    constexpr unsigned backends=4, rounds=6;
#elif defined(GEOPUTER_COMPARE_STDIO) || defined(GEOPUTER_COMPARE_RLE) || defined(GEOPUTER_COMPARE_RAW)
    constexpr unsigned backends=2, rounds=6;
#else
    constexpr unsigned backends=1, rounds=3;
#endif
#if !defined(GEOPUTER_COMPARE_STDIO) && !defined(GEOPUTER_COMPARE_RLE) && !defined(GEOPUTER_COMPARE_SPAN_FILL) && !defined(GEOPUTER_COMPARE_RAW)
    const unsigned order[3][3] = {{0,1,2},{2,0,1},{1,2,0}};
#endif
#if defined(GEOPUTER_COMPARE_STDIO) || defined(GEOPUTER_COMPARE_RLE) || defined(GEOPUTER_COMPARE_SPAN_FILL) || defined(GEOPUTER_COMPARE_RAW)
    // Each scale is a separate workload. Run larger work areas first so the
    // optional shrink prototypes do not fragment a later normal-size request.
    for (unsigned kind : {0u,2u,1u}) for (unsigned round=0;round<rounds;++round)
#else
    for (unsigned round=0;round<rounds;++round) for (unsigned kind:order[round%3])
#endif
    for(unsigned slot=0;slot<backends;++slot) {
        const unsigned backend=(slot+round)%backends;
#ifdef GEOPUTER_COMPARE_SPAN_FILL
        const char* names[]={"both","before","mono","clip"};
        const char* backend_name=names[backend];
#elif defined(GEOPUTER_COMPARE_RAW)
        const char* backend_name=backend?"before":"rows";
#elif defined(GEOPUTER_COMPARE_RLE)
        const char* backend_name=backend?"scalar":"span";
#else
        const char* backend_name=backend?"stdio":"fatfs";
#endif
        auto read_rect=[&](const galuchat::Rect& r,uint32_t* pixels) {
#ifdef GEOPUTER_COMPARE_SPAN_FILL
            if(backend==1) before_reader.readRect(r,pixels);
            else if(backend==2) mono_reader.readRect(r,pixels);
            else if(backend==3) clip_reader.readRect(r,pixels);
            else reader.readRect(r,pixels);
#elif defined(GEOPUTER_COMPARE_RAW)
            if(backend)raw_before_reader.readRect(r,pixels);else reader.readRect(r,pixels);
#elif defined(GEOPUTER_COMPARE_RLE)
            if(backend) scalar_reader.readRect(r,pixels);
            else reader.readRect(r,pixels);
#elif defined(GEOPUTER_COMPARE_STDIO)
            (backend?stdio_reader:reader).readRect(r,pixels);
#else
            reader.readRect(r,pixels);
#endif
        };
        print("[PIPE-START] backend=%s round=%u scale=%u heap=%u largest=%u\n",backend_name,round+1,kind==0?1:kind==1?2:4,
            unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
        map_io={}; index_io={}; observer.reset();
#ifdef GEOPUTER_COMPARE_RAW
        galuchat::detail::raw_benchmark_cells=0;galuchat::detail::raw_benchmark_pixels=0;galuchat::detail::raw_benchmark_target=0;
#endif
        DecodeProfile decode{&map_io,&index_io};
        ActivateDecodeProfile activate(decode);
        uint64_t read_us=0, extract_us=0, old_us=0, yield_us=0, hash=0;
        uint64_t pipeline_us=0, compare_us=0;
        unsigned read_calls=0;
        size_t work_bytes=0;
        const auto started=monotonicMicros();
        {
            map::BoundaryImage out;
            if (kind==0) {
                work_bytes=size_t(normal.source.width)*normal.source.height*4;
                std::unique_ptr<uint32_t[]> pixels(new uint32_t[work_bytes/4]);
                observer.sample();
                auto tick=monotonicMicros();
                read_rect({normal.source.x,normal.source.y,normal.source.width,normal.source.height},pixels.get());
                read_us=monotonicMicros()-tick; read_calls=1;
                observer.sample();
                const map::RasterView view{pixels.get(),normal.source,work_bytes/4};
                current=view.get(normal.base.center_x,normal.base.center_y);
                if (!out.resize(240,101)) throw std::bad_alloc();
                // Alternate old/new order, same source and output allocation.
                // Keep only the resulting hash during comparison; no third UI image.
                uint64_t compare_hash=0;
                for (unsigned pass=0;pass<2;++pass) {
                    const bool before=((round+pass)%2)==0;
                    tick=monotonicMicros();
                    for (unsigned row=0;row<101;row+=config::MapViewRowsPerStep) {
                        const unsigned rows=std::min<unsigned>(config::MapViewRowsPerStep,101-row);
                        const auto error=before ? map::buildBoundaryRowsBefore(view,normal.base,current,row,rows,out)
                                                : map::buildBoundaryRows(view,normal.base,current,row,rows,out);
                        if(error!=map::Error::None) throw std::runtime_error("pipeline extraction");
                    }
                    const auto elapsed=monotonicMicros()-tick;
                    if(before)old_us=elapsed;else extract_us=elapsed;
                    const auto fingerprint=boundaryHash(out);
                    if(pass && fingerprint!=compare_hash)throw std::runtime_error("old/new boundary image mismatch");
                    compare_hash=fingerprint;
                    if(before)compare_us=monotonicMicros()-tick;
                }
                hash=compare_hash;
                // Both passes were executed, so total below is accounted by phases.
            } else {
                const auto stats=renderStrips(kind==1?half:quarter,current,16,out,
                    [&](const galuchat::Rect& r,uint32_t* dst){observer.sample();read_rect(r,dst);observer.sample();},
                    [&]{observer.sample();vTaskDelay(1);});
                read_us=stats.read_us;extract_us=stats.extract_us;yield_us=stats.yield_us;
                read_calls=stats.calls;work_bytes=stats.work_bytes;
                hash=boundaryHash(out);
            }
            observer.sample();
        }
        pipeline_us=monotonicMicros()-started-compare_us;
        if(have_hash[kind] && hashes[kind]!=hash)throw std::runtime_error("pipeline round image mismatch");
        hashes[kind]=hash;have_hash[kind]=true;
#ifdef GEOPUTER_COMPARE_RAW
        print("[RAW-PIPE] backend=%s round=%u scale=%u cells=%u pixels=%llu target=%llu\n",backend_name,round+1,kind==0?1:kind==1?2:4,
            galuchat::detail::raw_benchmark_cells,(unsigned long long)galuchat::detail::raw_benchmark_pixels,
            (unsigned long long)galuchat::detail::raw_benchmark_target);
#endif
        const auto file_us=map_io.elapsed_us+index_io.elapsed_us;
        if(decode.io_us>decode.inclusive_us || file_us+decode.inclusive_us-decode.io_us>read_us)
            throw std::runtime_error("pipeline timing overlap");
        const auto decode_us=decode.inclusive_us-decode.io_us;
        const auto index_other_us=read_us-file_us-decode_us;
        print("[PIPE-CASE] backend=%s round=%u scale=%u band=%u total_us=%llu readrect_us=%llu file_us=%llu decode_us=%llu index_other_us=%llu extract_us=%llu old_extract_us=%llu yield_us=%llu read_calls=%u decode_calls=%u work=%u hash=%llu heap_min=%u\n",
            backend_name,round+1,kind==0?1:kind==1?2:4,kind==0?0:16,
            (unsigned long long)pipeline_us,(unsigned long long)read_us,(unsigned long long)file_us,
            (unsigned long long)decode_us,(unsigned long long)index_other_us,(unsigned long long)extract_us,
            (unsigned long long)old_us,(unsigned long long)yield_us,read_calls,decode.calls,(unsigned)work_bytes,
            (unsigned long long)hash,(unsigned)observer.minimum);
        auto io=[&](const char* file,const FileIoTiming& t){print("[PIPE-IO] backend=%s round=%u scale=%u file=%s bytes=%llu reads=%u seeks=%u opens=%u closes=%u read_us=%llu seek_us=%llu open_us=%llu close_us=%llu stat_us=%llu\n",
            backend_name,round+1,kind==0?1:kind==1?2:4,file,(unsigned long long)t.bytes,t.reads,t.seeks,t.opens,t.closes,
            (unsigned long long)t.read_us,(unsigned long long)t.seek_us,(unsigned long long)t.open_us,
            (unsigned long long)t.close_us,(unsigned long long)t.stat_us);};
        io("map",map_io);io("index",index_io);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    print("[PIPE-DONE] cases=%u old_new_equal=1 all_rounds_equal=1\n",3*rounds*backends);
}
} // namespace geoputer::benchmark

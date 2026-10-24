"""Isolate the old RAW decoder in a temporary diagnostic, sharing all I/O code."""
import shutil

def prepare_raw_comparison(root,work,source_path,source,wait_background=False):
    headers=work/"include/galuchat"
    shutil.copytree(root/"third_party/galuchat/include/galuchat",headers,dirs_exist_ok=True)
    path=headers/"wgsmap3_reader.hpp";text=path.read_text()
    marker="// Optional capability shared by RLE and constant fills."
    assert text.count(marker)==1
    traits="""// Diagnostic-only RAW dispatch and census. No production header is edited.
template<class Raster,class=void> struct RawBeforeBenchmark : std::false_type {};
template<class Raster> struct RawBeforeBenchmark<Raster,std::void_t<decltype(Raster::benchmark_raw_before)>>
    : std::bool_constant<Raster::benchmark_raw_before> {};
inline uint32_t raw_benchmark_cells=0,raw_benchmark_before_calls=0;
inline uint64_t raw_benchmark_pixels=0,raw_benchmark_target=0;
"""
    text=text.replace(marker,traits+marker,1)
    at=text.index("    template <class Raster, class ReadValue>\n    static void readRawRows(")
    frozen=(root/"tests/performance/baseline/RawBeforeRows.inc").read_text()
    frozen=frozen[frozen.index("    template <class Raster>"):].replace("void readRawRect(","void readRawRectBeforeRows(",1)
    text=text[:at]+frozen+"\n"+text[at:]
    start=text.index("    void readRawRect(")
    at=text.index("        if (!header.hasValidRawReservedBits())",start)
    text=text[:at]+"""        ++detail::raw_benchmark_cells;
        detail::raw_benchmark_pixels+=uint64_t(resolution)*resolution;
        detail::raw_benchmark_target+=uint64_t(target.width)*target.height;
        if constexpr(detail::RawBeforeBenchmark<Raster>::value) {
            ++detail::raw_benchmark_before_calls;
            readRawRectBeforeRows(header,reader,resolution,pallet_reader,pmgr,target,dest,dest_x,dest_y);
            return;
        }
"""+text[at:]
    path.write_text(text)
    variant=source.replace("DiskIndexedMapReader","RawBeforeMapReader")
    assert variant.count("struct Destination {")==1
    variant=variant.replace("struct Destination {","struct Destination {\n    static constexpr bool benchmark_raw_before=true;",1)
    source_path.with_name("RawBeforeMapReader.cpp").write_text(variant)
    source_path.with_name("RawBeforeMapReader.h").write_text(source_path.with_suffix(".h").read_text().replace("DiskIndexedMapReader","RawBeforeMapReader"))
    shutil.copyfile(root/"tests/performance/RawCellBenchmark.h",work/"application/benchmark/RawCellBenchmark.h")
    if wait_background:
        p=work/"application/benchmark/DecodeProfile.h";s=p.read_text().replace("#pragma once","#pragma once\n#include <atomic>",1)
        s=s.replace("namespace geoputer::benchmark {","namespace geoputer::benchmark {\ninline std::atomic<uint32_t> background_since_ms{0};",1);p.write_text(s)
        p=work/"application/task/ui/UiTask.cpp";s=p.read_text().replace('#include "UiTask.h"','#include "UiTask.h"\n#include "../../benchmark/DecodeProfile.h"',1)
        needle="        if (ready && model_.takeRedraw()) renderer.draw(model_);";assert s.count(needle)==1
        s=s.replace(needle,needle+"""
        if(ready && model_.mode()==DisplayMode::Background) {
            if(!benchmark::background_since_ms.load(std::memory_order_relaxed))
                benchmark::background_since_ms.store(now,std::memory_order_relaxed);
        } else benchmark::background_since_ms.store(0,std::memory_order_relaxed);
""");p.write_text(s)

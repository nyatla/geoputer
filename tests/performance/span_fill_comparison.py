"""Generate four diagnostic destinations; production has no comparison switches."""
import shutil

def prepare_span_fill_comparison(root, work, source_path, source, wait_background=False):
    headers=work/'include/galuchat'
    shutil.copytree(root/'third_party/galuchat/include/galuchat',headers,dirs_exist_ok=True)
    path=headers/'wgsmap3_reader.hpp'
    text=path.read_text()
    start=text.index('template <class Raster>\ninline void paintRleRun(')
    end=text.index('\ntemplate <class Raster>\ninline void fillRect',start)
    # Preserve the measured experiment even after production rolls it back.
    optimized=(root/'tests/performance/baseline/RleSpanClipExperiment.inc').read_text()
    optimized=optimized[optimized.index('template <class Raster>'):]
    body=optimized.index('    const bool vertical')
    assert optimized.endswith('}\n')
    optimized=optimized[:body]+'''    if constexpr (!SpanBenchmarkFlags<Raster>::clip) {
        paintRleRunBeforeClip(dest,target,dest_x,dest_y,resolution,mode,position,count,value);
    } else {
'''+''.join('    '+line+'\n' for line in optimized[body:-2].splitlines())+'''    }
}
'''
    traits='''template<class Raster,class=void> struct SpanBenchmarkFlags {
    static constexpr bool mono=true,clip=true;
};
template<class Raster> struct SpanBenchmarkFlags<Raster,std::void_t<
    decltype(Raster::benchmark_mono),decltype(Raster::benchmark_clip)>> {
    static constexpr bool mono=Raster::benchmark_mono,clip=Raster::benchmark_clip;
};
'''
    before=(root/'tests/performance/baseline/RleSpanBeforeClip.inc').read_text()
    text=text[:start]+traits+before+optimized+text[end:]
    fill=text.index('inline void fillRect(')
    prefix,suffix=text[:fill],text[fill:]
    assert suffix.count('if constexpr (HasSpan<Raster>::value)')==1
    suffix=suffix.replace('if constexpr (HasSpan<Raster>::value)',
                          'if constexpr (HasSpan<Raster>::value && SpanBenchmarkFlags<Raster>::mono)',1)
    path.write_text(prefix+suffix)
    header=source_path.with_suffix('.h').read_text()
    for name,mono,clip in [('BeforeSpanMapReader',False,False),('MonoSpanMapReader',True,False),('ClipSpanMapReader',False,True)]:
        flags=f'static constexpr bool benchmark_mono={str(mono).lower()},benchmark_clip={str(clip).lower()};'
        variant=source.replace('DiskIndexedMapReader',name)
        assert variant.count('struct Destination {')==1
        variant=variant.replace('struct Destination {','struct Destination {\n    '+flags)
        source_path.with_name(name+'.cpp').write_text(variant)
        source_path.with_name(name+'.h').write_text(header.replace('DiskIndexedMapReader',name))

    if wait_background:
        profile=work/'application/benchmark/DecodeProfile.h'
        text=profile.read_text().replace('#pragma once','#pragma once\n#include <atomic>',1)
        text=text.replace('namespace geoputer::benchmark {','namespace geoputer::benchmark {\ninline std::atomic<uint32_t> background_since_ms{0};',1)
        profile.write_text(text)
        ui=work/'application/task/ui/UiTask.cpp'
        text=ui.read_text().replace('#include "UiTask.h"','#include "UiTask.h"\n#include "../../benchmark/DecodeProfile.h"',1)
        needle='        if (ready && model_.takeRedraw()) renderer.draw(model_);'
        assert text.count(needle)==1
        text=text.replace(needle,needle+'''
        if(ready && model_.mode()==DisplayMode::Background) {
            if(!benchmark::background_since_ms.load(std::memory_order_relaxed))
                benchmark::background_since_ms.store(now,std::memory_order_relaxed);
        } else benchmark::background_since_ms.store(0,std::memory_order_relaxed);
''')
        ui.write_text(text)

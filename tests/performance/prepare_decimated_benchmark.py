# Prepare an isolated app-only decimate-then-extract hardware comparison.
from pathlib import Path
import shutil
root=Path(__file__).resolve().parents[2];work=root/'.pio/decimated-map';work.mkdir(parents=True,exist_ok=True)
shutil.copytree(root/'application',work/'application',dirs_exist_ok=True)
bench=work/'application/benchmark';bench.mkdir(exist_ok=True)
for name in ['DecimatedMapBenchmark.h','QuarterScaleBenchmark.h','ScaledBoundaryPrototype.h','DecodeProfile.h']:
    shutil.copyfile(root/'tests/performance'/name,bench/name)
p=work/'application/task/storage/galuchat/DiskIndexedMapReader.cpp';s=p.read_text().replace('#include "DiskIndexedMapReader.h"','#include "DiskIndexedMapReader.h"\n#include "../../../benchmark/DecodeProfile.h"')
s=s.replace('            auto stream = map.createRange(start, bytes);','            benchmark::DecodeScope decode_scope;\n            auto stream = map.createRange(start, bytes);')
s=s.replace('                galuchat::GaluchatImageDataChunk01Reader::readRectFromBlockRange(',
            '                { benchmark::DecodeScope decode_scope;\n                galuchat::GaluchatImageDataChunk01Reader::readRectFromBlockRange(')
s=s.replace('*stream, {int(width), int(height), int(square), number, count, length}, x, y, view);',
            '*stream, {int(width), int(height), int(square), number, count, length}, x, y, view); }')
assert s.count('benchmark::DecodeScope decode_scope;')==2;p.write_text(s)
p=work/'application/task/ui/UiTask.cpp';s=p.read_text().replace('#include "UiTask.h"','#include "UiTask.h"\n#include "../../benchmark/DecimatedMapBenchmark.h"',1)
# Include just the atomic latch, not the storage diagnostic, in the UI TU.
s=s.replace('#include "../../benchmark/DecimatedMapBenchmark.h"','#include <atomic>\nnamespace geoputer::benchmark { extern std::atomic<uint32_t> decimation_background_ms; }')
needle='        if (ready && model_.takeRedraw()) renderer.draw(model_);';assert s.count(needle)==1
s=s.replace(needle,needle+'''
        if(ready && model_.mode()==DisplayMode::Background) {
            if(!benchmark::decimation_background_ms.load(std::memory_order_relaxed))
                benchmark::decimation_background_ms.store(now,std::memory_order_relaxed);
        } else benchmark::decimation_background_ms.store(0,std::memory_order_relaxed);
''');p.write_text(s)
p=work/'application/task/storage/StorageTask.cpp';s=p.read_text().replace('#include "StorageTask.h"','#include "StorageTask.h"\n#include "../../benchmark/DecimatedMapBenchmark.h"')
at=s.index('        vTaskDelay(pdMS_TO_TICKS(5));',s.index('void StorageTask::run()'))
s=s[:at]+'''
        static bool decimation_ran=false;
        const auto background=benchmark::decimation_background_ms.load(std::memory_order_relaxed);
        if(!decimation_ran && !usb_mode_ && initialized_ && !hasJob() && !has_completion_ && !has_return_ &&
           state_.card_state==CardState::Mounted && place_lookup_.cacheStats().valid && place_request_.fix.valid &&
           background && taskTimeMs()-background>=2000) {
            decimation_ran=true;
            Console.printf("[DECIM-DISPLAY] background_ms=%lu now_ms=%lu timeout_s=%u\\n",static_cast<unsigned long>(background),
                           static_cast<unsigned long>(taskTimeMs()),ScreenTimeouts[state_.settings.screen_timeout]);
            Console.printf("[SD-CLOCK] requested=%lu actual=%lu\\n",static_cast<unsigned long>(config::SdSpiHz),
                           static_cast<unsigned long>(spiClockDivToFrequency(spi_.getClockDivider())));
            try {
                benchmark::runDecimatedMapBenchmark(DatasetCatalog{}.directory(state_.settings.dataset),state_.settings.wordbook_language,drive_,this,
                    [](const char* format,auto... values){Console.printf(format,values...);});
            }catch(const std::exception& error){Console.printf("[DECIM-ERROR] %s\\n",error.what());}
        }
'''+s[at:];p.write_text(s)
s=(root/'platformio.ini').read_text().replace('src_dir = application','src_dir = .pio/decimated-map/application\nbuild_dir = .pio/decimated-map/build')
(root/'.pio/decimated-map.ini').write_text(s)
for name in ['flash.py','capture.py','run_device.py','run_job.py']:
    shutil.copyfile(root/'.pio/raw-device'/name,work/name)
print('Prepared',work)

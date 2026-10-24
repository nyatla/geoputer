"""Build an isolated map I/O/decode/extraction diagnostic; never edit normal sources."""
from pathlib import Path
import shutil

root=Path(__file__).resolve().parents[2]
work=root/'.pio/map-pipeline'
work.mkdir(parents=True,exist_ok=True)
shutil.copytree(root/'application',work/'application',dirs_exist_ok=True)
bench=work/'application/benchmark'
bench.mkdir(exist_ok=True)
for name in ['MapPipelineBenchmark.h','DecodeProfile.h','QuarterScaleBenchmark.h','ScaledBoundaryPrototype.h']:
    shutil.copyfile(root/'tests/performance'/name,bench/name)
shutil.copyfile(root/'tests/performance/baseline/BoundaryExtractorBefore.h',bench/'BoundaryExtractorBefore.h')
p=work/'application/task/storage/galuchat/DiskIndexedMapReader.cpp'
s=p.read_text().replace('#include "DiskIndexedMapReader.h"','#include "DiskIndexedMapReader.h"\n#include "../../../benchmark/DecodeProfile.h"')
s=s.replace('''            auto stream = map_->createRange(start, bytes);''','''            benchmark::DecodeScope decode_scope;
            auto stream = map_->createRange(start, bytes);''')
s=s.replace('''                galuchat::GaluchatImageDataChunk01Reader::readRectFromBlockRange(''','''                { benchmark::DecodeScope decode_scope;
                galuchat::GaluchatImageDataChunk01Reader::readRectFromBlockRange(''')
s=s.replace('''*stream, {int(width), int(height), int(square), number, count, length}, x, y, view);''','''*stream, {int(width), int(height), int(square), number, count, length}, x, y, view); }''')
assert s.count('benchmark::DecodeScope decode_scope;')==2
p.write_text(s)
p=work/'application/task/storage/StorageTask.cpp'
s=p.read_text().replace('#include "StorageTask.h"','#include "StorageTask.h"\n#include "../../benchmark/MapPipelineBenchmark.h"')
at=s.index('        vTaskDelay(pdMS_TO_TICKS(5));',s.index('void StorageTask::run()'))
s=s[:at]+'''        static bool pipeline_benchmark_ran=false;
        if(!pipeline_benchmark_ran && !usb_mode_ && initialized_ && !hasJob() && !has_completion_ && !has_return_ &&
           state_.card_state==CardState::Mounted && place_lookup_.cacheStats().valid && place_request_.fix.valid) {
            pipeline_benchmark_ran=true;
            Console.printf("[SD-CLOCK] requested=%lu actual=%lu divider=0x%08lX apb=%lu\\n",
                static_cast<unsigned long>(config::SdSpiHz), static_cast<unsigned long>(spiClockDivToFrequency(spi_.getClockDivider())),
                static_cast<unsigned long>(spi_.getClockDivider()), static_cast<unsigned long>(getApbFrequency()));
            try {
                benchmark::runMapPipelineBenchmark(DatasetCatalog{}.directory(state_.settings.dataset),state_.settings.wordbook_language,drive_,this,
                    [](const char* format,auto... values){Console.printf(format,values...);});
            }catch(const std::exception& error){Console.printf("[PIPE-ERROR] %s\\n",error.what());}
        }
'''+s[at:]
p.write_text(s)
s=(root/'platformio.ini').read_text().replace('src_dir = application','src_dir = .pio/map-pipeline/application\nbuild_dir = .pio/map-pipeline/build')
(root/'.pio/map-pipeline.ini').write_text(s)
for name in ['flash.py','capture.py']:
    shutil.copyfile(root/'.pio/map-heap'/name,work/name)
print('Prepared',work)
print('Build: pio run -c .pio/map-pipeline.ini -e cardputer-adv')

"""Build an isolated map I/O/decode/extraction diagnostic; never edit normal sources."""
from pathlib import Path
import shutil
import argparse
import re

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--name',default='map-pipeline',help='Artifact directory name under .pio')
parser.add_argument('--compare-stdio',action='store_true',help='Compare with the pre-FatFs Reader in the same firmware')
args=parser.parse_args()
if not re.fullmatch(r'[a-z0-9-]+',args.name): parser.error('Invalid artifact name')
work=root/'.pio'/args.name
work.mkdir(parents=True,exist_ok=True)
shutil.copytree(root/'application',work/'application',dirs_exist_ok=True)
bench=work/'application/benchmark'
bench.mkdir(exist_ok=True)
for name in ['MapPipelineBenchmark.h','DecodeProfile.h','QuarterScaleBenchmark.h','ScaledBoundaryPrototype.h']:
    shutil.copyfile(root/'tests/performance'/name,bench/name)
shutil.copyfile(root/'tests/performance/baseline/BoundaryExtractorBefore.h',bench/'BoundaryExtractorBefore.h')
if args.compare_stdio:
    shutil.copyfile(root/'tests/performance/baseline/ObservedFileReaderStdio.h',bench/'ObservedFileReaderStdio.h')
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
s=(root/'platformio.ini').read_text().replace('src_dir = application',f'src_dir = .pio/{args.name}/application\nbuild_dir = .pio/{args.name}/build')
if args.compare_stdio: s=s.replace('    -Wall','    -Wall\n    -DGEOPUTER_COMPARE_STDIO')
(root/'.pio'/f'{args.name}.ini').write_text(s)
for name in ['flash.py','capture.py']:
    shutil.copyfile(root/'.pio/map-heap'/name,work/name)
print('Prepared',work)
print(f'Build: pio run -c .pio/{args.name}.ini -e cardputer-adv')

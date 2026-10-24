"""Create an isolated, once-per-boot scaled-map diagnostic firmware source tree.

Normal application sources and platformio.ini are never modified. After measuring,
restore the normal firmware. Upload/capture helpers are optional local artifacts.
"""
from pathlib import Path
import argparse
import math
import shutil
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--coordinate',nargs=2,type=float,metavar=('LAT','LON'),help='Freeze the benchmark at a previous measurement coordinate')
args=parser.parse_args()
if args.coordinate and not (all(math.isfinite(v) for v in args.coordinate) and -90 < args.coordinate[0] < 90 and -180 <= args.coordinate[1] <= 180):
    parser.error('Coordinate is outside the supported range')
root=Path(__file__).resolve().parents[2];work=root/'.pio/quarter-scale';work.mkdir(parents=True,exist_ok=True)
shutil.copytree(root/'application',work/'application',dirs_exist_ok=True)
bench=work/'application/benchmark';bench.mkdir(exist_ok=True)
for name in ['QuarterScaleBenchmark.h','ScaledBoundaryPrototype.h']:shutil.copyfile(root/'tests/performance'/name,bench/name)
p=work/'application/task/storage/StorageTask.cpp';s=p.read_text().replace('#include "StorageTask.h"','#include "StorageTask.h"\n#include "../../benchmark/QuarterScaleBenchmark.h"')
at=s.index('        vTaskDelay(pdMS_TO_TICKS(5));',s.index('void StorageTask::run()'))
s=s[:at]+'''        static bool scale_benchmark_ran=false;
        if(!scale_benchmark_ran && !usb_mode_ && initialized_ && !hasJob() && !has_completion_ && !has_return_ &&
           state_.card_state==CardState::Mounted && place_lookup_.cacheStats().valid && place_request_.fix.valid) {
            scale_benchmark_ran=true;
            Console.printf("[SD-CLOCK] requested=%lu actual=%lu divider=0x%08lX apb=%lu\\n",
                static_cast<unsigned long>(config::SdSpiHz), static_cast<unsigned long>(spiClockDivToFrequency(spi_.getClockDivider())),
                static_cast<unsigned long>(spi_.getClockDivider()), static_cast<unsigned long>(getApbFrequency()));
            try {
                benchmark::runQuarterScaleBenchmark(DatasetCatalog{}.directory(state_.settings.dataset),state_.settings.wordbook_language,drive_,
                    place_request_.fix.latitude,place_request_.fix.longitude,this,
                    [](const char* format,auto... values){Console.printf(format,values...);});
            }catch(const std::exception& error){Console.printf("[QUARTER-ERROR] %s\\n",error.what());}
        }
'''+s[at:]
if args.coordinate:
    s=s.replace('place_request_.fix.latitude,place_request_.fix.longitude,this,',f'{args.coordinate[0]:.9f},{args.coordinate[1]:.9f},this,')
p.write_text(s)
s=(root/'platformio.ini').read_text().replace('src_dir = application','src_dir = .pio/quarter-scale/application\nbuild_dir = .pio/quarter-scale/build')
(root/'.pio/quarter-scale.ini').write_text(s)
# Use the already validated upload/capture wrappers; Windows dependencies are shared.
for name in ['flash.py','capture.py']:
    helper=root/'.pio/map-heap'/name
    if helper.is_file():shutil.copyfile(helper,work/name)
print('Prepared',work)
print('Build: pio run -c .pio/quarter-scale.ini -e cardputer-adv')

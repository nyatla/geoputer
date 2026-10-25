"""Create an isolated, once-per-boot scaled-map diagnostic firmware source tree.

Normal application sources and platformio.ini are never modified. After measuring,
restore the normal firmware. Upload/capture helpers are optional local artifacts.
"""
from pathlib import Path
from benchmark_support import (prepare_workspace, inject_storage_probe, diagnostic_config, copy_device_helpers)
import argparse
import math
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--coordinate',nargs=2,type=float,metavar=('LAT','LON'),help='Freeze the benchmark at a previous measurement coordinate')
args=parser.parse_args()
if args.coordinate and not (all(math.isfinite(v) for v in args.coordinate) and -90 < args.coordinate[0] < 90 and -180 <= args.coordinate[1] <= 180):
    parser.error('Coordinate is outside the supported range')
root=Path(__file__).resolve().parents[2]
work=prepare_workspace(root,'quarter-scale',['QuarterScaleBenchmark.h', 'ScaledBoundaryPrototype.h'])
p=work/'application/task/storage/StorageTask.cpp';s=p.read_text()
s=inject_storage_probe(s,'QuarterScaleBenchmark.h','''        static bool scale_benchmark_ran=false;
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
''')
if args.coordinate:
    s=s.replace('place_request_.fix.latitude,place_request_.fix.longitude,this,',f'{args.coordinate[0]:.9f},{args.coordinate[1]:.9f},this,')
p.write_text(s)
s=diagnostic_config(root,work)
(root/'.pio/quarter-scale.ini').write_text(s)
copy_device_helpers(root,work)
print('Prepared',work)
print('Build: pio run -c .pio/quarter-scale.ini -e cardputer-adv')

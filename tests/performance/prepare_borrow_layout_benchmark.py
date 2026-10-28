"""Prepare a read-only on-device AB comparison of borrowed workspace layouts."""
from pathlib import Path
import argparse
import shutil
from benchmark_support import prepare_workspace, replace_once, copy_device_helpers

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--name',default='borrow-layout-bench')
args=parser.parse_args()
work=prepare_workspace(root,args.name,['BorrowLayoutBenchmark.h'])
app=work/'application'
shutil.copyfile(root/'tests/performance/baseline/MapBuildJobBeforeBorrowedLayout.h',app/'benchmark/BeforeMapBuildJob.h')
path=app/'task/storage/StorageTask.cpp'
source=replace_once(path.read_text(),'#include "StorageTask.h"','#include "StorageTask.h"\n#include "../../benchmark/BorrowLayoutBenchmark.h"')
needle='        const bool statistics=has_read_ && screen_resources_.work.logs().result().request.kind==LogReadKind::Statistics;'
body=r'''        static bool layout_bench_ran=false;
        if(!layout_bench_ran && taskTimeMs()>6000 && !usb_mode_ && initialized_ && !hasJob() && replies_.idle() && !has_read_ &&
           !screen_resources_.work.mapBusy() && state_.card_state==CardState::Mounted && !log_writer_.hasRetainedSession()) {
            layout_bench_ran=true;unsigned cases=0,datasets=0;bool failed=false;
            Console.println("[BORROW-LAYOUT-BEGIN] AB alternating, warm round=0, no NVS/log writes");
            try {
                for(unsigned number=1;number<=99;++number) {
                    const auto directory=DatasetCatalog{}.directory(number);struct stat st{};
                    if(stat(directory.c_str(),&st) || !S_ISDIR(st.st_mode))continue;
                    place_lookup_.configure(directory,"");
                    const auto prepared=place_lookup_.prepare();
                    if(prepared.state!=PlaceState::Ready) {
                        Console.printf("[BORROW-LAYOUT-SKIP] dataset=%02u state=%u\n",number,unsigned(prepared.state));continue;
                    }
                    ++datasets;
                    cases+=benchmark::runBorrowLayout(selected_dataset_,code_images_.map,drive_,number,
                        [](const char* f,auto... values){Console.printf(f,values...);});
                }
            }catch(const std::exception& error){failed=true;Console.printf("[BORROW-LAYOUT-ERROR] %s\n",error.what());}
            configureDataset();(void)place_lookup_.prepare();
            Console.printf("[BORROW-LAYOUT-DONE] %s datasets=%u cases=%u\n",failed?"FAILED":"OK",datasets,cases);
        }
'''
path.write_text(replace_once(source,needle,body+needle))
# Normal diagnostics remain disabled so formatted output is outside timing.
config=replace_once((root/'platformio.ini').read_text(),'src_dir = application',
    f'src_dir = .pio/{args.name}/application\nbuild_dir = .pio/{args.name}/build\nlibdeps_dir = .pio/libdeps')
(root/'.pio'/f'{args.name}.ini').write_text(config)
copy_device_helpers(root,work)
print('Prepared',work)

"""Prepare an isolated read-only code-cache size comparison for the real SD."""
from pathlib import Path
import shutil
root=Path(__file__).resolve().parents[2]
work=root/'.pio/code-cache-bench';work.mkdir(parents=True,exist_ok=True)
shutil.copytree(root/'application',work/'application',dirs_exist_ok=True)
bench=work/'application/benchmark';bench.mkdir(exist_ok=True)
shutil.copyfile(root/'tests/performance/CodeCacheBenchmark.h',bench/'CodeCacheBenchmark.h')
p=work/'application/task/storage/StorageTask.cpp'
s=p.read_text().replace('#include "StorageTask.h"','#include "StorageTask.h"\n#include "../../benchmark/CodeCacheBenchmark.h"',1)
needle='        vTaskDelay(pdMS_TO_TICKS(5));'
pos=s.index(needle,s.index('void StorageTask::run()'))
s=s[:pos]+'''        static bool cache_benchmark_ran=false;
        if(!cache_benchmark_ran && !usb_mode_ && initialized_ && !hasJob() && !has_completion_ && !has_return_ &&
           !map_job_.occupied() && !has_read_ && state_.card_state==CardState::Mounted && place_lookup_.datasetInfo().state==PlaceState::Ready) {
            cache_benchmark_ran=true;
            // No retained main cache during the trial: each test creates exactly
            // one code cache. This temporary diagnostic will be replaced after capture.
            place_lookup_.close();
            try {benchmark::runCodeCacheBenchmark(drive_,this,[](const char* format,auto... values){Console.printf(format,values...);});}
            catch(const std::exception& error){Console.printf("[CODE-CACHE-ERROR] %s\\n",error.what());}
        }
'''+s[pos:];p.write_text(s)
s=(root/'platformio.ini').read_text().replace('src_dir = application','src_dir = .pio/code-cache-bench/application\nlibdeps_dir = .pio/libdeps')
# Reuse the existing framework objects; the normal app is rebuilt and restored afterwards.
(root/'.pio/code-cache-bench.ini').write_text(s)
print('Prepared',work)

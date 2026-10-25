"""Prepare an isolated read-only code-cache size comparison for the real SD."""
from pathlib import Path
from benchmark_support import (prepare_workspace, inject_storage_probe, diagnostic_config, copy_device_helpers)
root=Path(__file__).resolve().parents[2]
work=prepare_workspace(root,'code-cache-bench',['CodeCacheBenchmark.h'])
p=work/'application/task/storage/StorageTask.cpp';s=p.read_text()
s=inject_storage_probe(s,'CodeCacheBenchmark.h','''        static bool cache_benchmark_ran=false;
        if(!cache_benchmark_ran && !usb_mode_ && initialized_ && !hasJob() && !has_completion_ && !has_return_ &&
           !map_job_.occupied() && !has_read_ && state_.card_state==CardState::Mounted && place_lookup_.datasetInfo().state==PlaceState::Ready) {
            cache_benchmark_ran=true;
            // No retained main cache during the trial: each test creates exactly
            // one code cache. This temporary diagnostic will be replaced after capture.
            place_lookup_.close();
            try {benchmark::runCodeCacheBenchmark(drive_,this,[](const char* format,auto... values){Console.printf(format,values...);});}
            catch(const std::exception& error){Console.printf("[CODE-CACHE-ERROR] %s\\n",error.what());}
        }
''');p.write_text(s)
s=diagnostic_config(root,work)
(root/'.pio/code-cache-bench.ini').write_text(s)
copy_device_helpers(root,work)
print('Prepared',work)

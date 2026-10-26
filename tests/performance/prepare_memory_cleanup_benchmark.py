"""Prepare isolated before/after runs; no log writes or settings changes."""
from pathlib import Path
import argparse,shutil,sys
p=argparse.ArgumentParser(description=__doc__);p.add_argument('label',choices=['before','after']);p.add_argument('--source',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests/performance'))
from benchmark_support import diagnostic_config,replace_once
work=root/'.pio'/('memory-cleanup-'+a.label);app=work/'application'
work.mkdir(parents=True,exist_ok=True)
if app.exists():shutil.rmtree(app)
shutil.copytree((a.source or root)/'application',app)
(app/'benchmark').mkdir(exist_ok=True)
probe=(root/'tests/performance/MapSessionBenchmark.inc').read_text()+'''
if(bench_stage==10) {
    static unsigned verified=0;
    static uint32_t verify_started=0;
    if(verify_started && !dataset_waiting_) {
        Console.printf("[MEMORY-VERIFY] run=%u error=%u app_ms=%lu\\n",verified+1,unsigned(data_.datasets().error),static_cast<unsigned long>(taskTimeMs()-verify_started));
        if(data_.datasets().error!=DatasetError::None) {Console.println("[MEMORY-BENCH-DONE] FAILED");bench_stage=11;}
        else ++verified;
        verify_started=0;
    }
    if(bench_stage==10 && !verify_started && !dataset_check_needed_ && !dataset_check_waiting_ && !data_.storage.busy && !place_flow_.pending()) {
        if(verified==3) {Console.println("[MEMORY-BENCH-DONE] OK");bench_stage=11;}
        else {
            if(screen_!=ScreenId::Settings)setScreen(ScreenId::Settings);
            DatasetRequest request{};request.action=DatasetAction::VerifyIndex;request.purpose=DatasetPurpose::Verify;
            request.index_target=IndexTarget::All;request.number=data_.settings.dataset;
            verify_started=taskTimeMs();
            if(!requestDataset(request)) {Console.println("[MEMORY-BENCH-DONE] REJECTED");bench_stage=11;}
        }
    }
}
'''
(app/'benchmark/MemoryCleanupBenchmark.inc').write_text(probe)
p=app/'Application.cpp';s=replace_once(p.read_text(),'#include "Application.h"','#include "Application.h"\n#include "utils/HeapDiagnostics.h"')
s=replace_once(s,'void Application::runOnce() {\n    drainNotifications();','void Application::runOnce() {\n    drainNotifications();\n#include "benchmark/MemoryCleanupBenchmark.inc"');p.write_text(s)
p=app/'task/storage/StorageTask.cpp';s=replace_once(p.read_text(),'    if (!releaseUnusableDataset(settings_store_, state_, reason)) return;','    Console.printf("[MEMORY-BENCH-ERROR] dataset reason=%u; NVS unchanged\\n",unsigned(reason));\n    place_lookup_.close();return;');p.write_text(s)
p=app/'task/storage/dataset/DatasetIndex.cpp';s=replace_once(p.read_text(),'#include "DatasetIndex.h"','#include "DatasetIndex.h"\n#include "../../../utils/HeapDiagnostics.h"')
s=replace_once(s,'    Output out(drive,path,observer,compare);','    if(compare)reportMapHeap("compare-before",0);\n    Output out(drive,path,observer,compare);\n    if(compare)reportMapHeap("compare-ready",0);');p.write_text(s)
(root/'.pio'/('memory-cleanup-'+a.label+'.ini')).write_text(diagnostic_config(root,work))
print(work)

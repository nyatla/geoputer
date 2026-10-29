from pathlib import Path
import shutil,sys,argparse,re
p=argparse.ArgumentParser();p.add_argument('--name');a=p.parse_args()
r=Path(__file__).resolve().parents[2];sys.path.insert(0,str(r/'tests/performance'))
from benchmark_support import diagnostic_config,replace_once
name=a.name or 'storage-work-current'
if not re.fullmatch(r'[a-z0-9-]+',name):p.error('Invalid benchmark name')
work=r/'.pio'/name;app=work/'application';work.mkdir(exist_ok=True)
if app.exists():shutil.rmtree(app)
shutil.copytree(r/'application',app);(app/'benchmark').mkdir(exist_ok=True)
shutil.copyfile(r/'tests/performance/StorageWorkMemoryProbe.h',app/'benchmark/WorkMemoryProbe.h')
probe=(r/'tests/performance/MapSessionBenchmark.inc').read_text().replace(' && data_.gps.fix.valid','')
probe=replace_once(probe,'bench_base=bench_fix=data_.gps.fix;bench_started=bench_now;bench_stage=1;',
    'bench_base=data_.gps.fix;bench_base.latitude=35.681641;bench_base.longitude=140.027194;bench_fix=bench_base;bench_started=bench_now;bench_stage=1;\n    if(!benchmark::begin()) {Console.println("[WORK-BENCH-DONE] PROBE FAILED");bench_stage=30;}')
probe=replace_once(probe,'if(bench_stage==10)Console.println("[SESSION-BENCH-DONE] OK");','if(bench_stage==10){Console.println("[SESSION-BENCH-DONE] OK");setScreen(ScreenId::Logs);}')
probe+='''
if(bench_stage>=10 && bench_stage<20) {
    if(taskTimeMs()-bench_started>150000) {Console.println("[WORK-BENCH-DONE] TIMEOUT");benchmark::dump();bench_stage=20;}
    else if(bench_stage==10 && screen_==ScreenId::Logs && !browse_waiting_ && !data_.browse().loading && !data_.storage.busy && !screen_resources_.logs().need) {
        if(!data_.browse().count) {Console.println("[WORK-BENCH] no logs; read-only list completed");bench_stage=19;}
        else {UiAction action{};action.browse_action=BrowseAction::OpenFile;action.browse_index=0;action.browse_revision=data_.browse().revision;
            if(operateBrowser(action))bench_stage=11;}
    } else if(bench_stage==11 && screen_==ScreenId::LogProperties && !browse_waiting_ && !data_.browse().loading && !screen_resources_.logs().need) {
        if(data_.browse().error!=LogReadError::None || !data_.browse().summary.records)bench_stage=19;
        else {UiAction action{};action.browse_action=BrowseAction::OpenPoint;operateBrowser(action);}
    } else if(bench_stage==11 && screen_==ScreenId::LogPoint && !browse_waiting_ && !data_.browse().loading && !screen_resources_.logs().need) {
        Console.printf("[WORK-BENCH] log records=%lu error=%u\\n",static_cast<unsigned long>(data_.browse().summary.records),unsigned(data_.browse().error));
        if(!data_.browse().summary.records || data_.browse().error!=LogReadError::None)bench_stage=19;
        else {UiAction action{};action.browse_action=BrowseAction::Map;if(operateBrowser(action))bench_stage=12;}
    } else if(bench_stage>=12 && bench_stage<=14 && !map_request_.token && !browse_waiting_ && !place_flow_.pending() && !data_.storage.busy && !data_.browse().loading && data_.browse().point_loaded && !data_.map.busy) {
        Console.printf("[WORK-BENCH] real-log step=%u point=%lu map_error=%u\\n",bench_stage,static_cast<unsigned long>(data_.browse().index),unsigned(data_.map.error));
        if(bench_stage==14)bench_stage=19;
        else {UiAction action{};action.browse_action=BrowseAction::Point;action.browse_index=std::min<uint32_t>(data_.browse().summary.records-1,data_.browse().index+10);
            if(action.browse_index==data_.browse().index)bench_stage=19;else if(operateBrowser(action))++bench_stage;}
    }
    if(bench_stage==19) {setScreen(ScreenId::LivePlace);bench_stage=20;}
}
if(bench_stage==20 && !map_request_.token && !map_session_.token && !browse_waiting_ && !data_.storage.busy && !place_flow_.pending()) {
    benchmark::dump();Console.println("[WORK-BENCH-DONE] OK");bench_stage=21;
}
'''
(app/'benchmark/StorageWorkBenchmark.inc').write_text(probe)
p=app/'Application.cpp';s=replace_once(p.read_text(),'#include "Application.h"','#include "Application.h"\n#include "utils/HeapDiagnostics.h"\n#include "benchmark/WorkMemoryProbe.h"');s=replace_once(s,'void Application::runOnce() {\n    drainNotifications();','void Application::runOnce() {\n    drainNotifications();\n#include "benchmark/StorageWorkBenchmark.inc"');p.write_text(s)
p=app/'utils/HeapDiagnostics.h';s=replace_once(p.read_text(),'#include "../Config.h"','#include "../Config.h"\n#include "../benchmark/WorkMemoryProbe.h"');s=replace_once(s,'    if (!config::MapHeapDiagnostics) return;','    if (!config::MapHeapDiagnostics) return;\n    benchmark::mark(stage);');p.write_text(s)
p=app/'task/storage/galuchat/ObservedFileReader.h';s=replace_once(p.read_text(),'#include "ReadTiming.h"','#include "ReadTiming.h"\n#include "../../../benchmark/WorkMemoryProbe.h"');s=replace_once(s,'        length_ = source_size - offset;','        length_ = source_size - offset;\n        benchmark::sample();');p.write_text(s)
p=app/'task/storage/log/LogReader.cpp';s=replace_once(p.read_text(),'#include "LogReader.h"','#include "LogReader.h"\n#include "../../../utils/HeapDiagnostics.h"');s=replace_once(s,'    result_={}; result_.request=request;','    reportMapHeap("log-read-begin",request.token);\n    result_={}; result_.request=request;');p.write_text(s)
p=app/'task/storage/StorageTask.cpp';s=replace_once(p.read_text(),'    if (!releaseUnusableDataset(settings_store_, state_, reason)) return;','    Console.printf("[WORK-BENCH-ERROR] dataset reason=%u; NVS unchanged\\n",unsigned(reason));\n    place_lookup_.close();return;');s=replace_once(s,'            const auto& command=request.payload.map;','            const auto& command=request.payload.map;\n            reportMapHeap("map-request",command.token);');s=replace_once(s,'    result.logs=read;','    result.logs=read;\n    reportMapHeap("log-read-done",read.request.token);');p.write_text(s)
(r/'.pio'/(name+'.ini')).write_text(diagnostic_config(r,work))
print(work)

p=app/"task/ui/UiModel.cpp";s=replace_once(p.read_text(),"const auto timeout = ScreenTimeouts[data_.settings.screen_timeout];","const auto timeout = 0u; // Automatic benchmark only; NVS unchanged.");p.write_text(s)

# Match the historical readRectDecimated measurement boundary.
p=app/"task/storage/galuchat/MapSource.cpp";s=replace_once(p.read_text(),'            reader.readRectDecimated({input.x,input.y,input.width,input.height},request.scale,result.pixels,work_buffer);','            const auto kernel_started=monotonicMicros();\n            reader.readRectDecimated({input.x,input.y,input.width,input.height},request.scale,result.pixels,work_buffer);\n            const auto kernel_us=monotonicMicros()-kernel_started;\n            Console.printf("[MAP-KERNEL] token=%lu scale=%u lat=%.6f lon=%.6f elapsed_us=%llu\\n",static_cast<unsigned long>(request.token),unsigned(request.scale),request.latitude,request.longitude,static_cast<unsigned long long>(kernel_us));');p.write_text(s)

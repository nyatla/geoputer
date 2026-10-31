"""Prepare isolated, target-safe log-statistics timing firmware; production is unchanged."""
from pathlib import Path
import sys,shutil
r=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(r/'tests/performance'))
from benchmark_support import diagnostic_config,replace_once
w=r/'.pio/log-statistics-bench';w.mkdir(exist_ok=True)
app=w/'application'
if app.exists():shutil.rmtree(app)
shutil.copytree(r/'application',app);(app/'benchmark').mkdir()
(app/'benchmark/StatisticsBenchmark.h').write_text(r'''#pragma once
#include <atomic>
#include <esp_heap_caps.h>
#include "../utils/Console.h"
#include "../task/storage/log/LogWriter.h"
namespace geoputer::benchmark {
inline std::atomic<int> ready{0},cleanup{0},mode{2};
inline std::atomic<uint32_t> min_heap{UINT32_MAX},min_largest{UINT32_MAX};
inline constexpr unsigned counts[]={1000,10000,100000};
inline bool created[3]{};
inline char record_name[48]{};inline uint64_t record_session=0;
inline void name(unsigned n,char* out,size_t bytes) {std::snprintf(out,bytes,"stats_probe_20261027_%u.gps.dat",counts[n]);}
inline void sample() {
    const auto heap=heap_caps_get_free_size(MALLOC_CAP_8BIT),largest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if(heap<min_heap.load())min_heap=heap;
    if(largest<min_largest.load())min_largest=largest;
}
inline void fixtures(uint8_t drive) {
    static LogFile file;file.setDrive(drive);
    for(unsigned n=0;n<3;++n) {
        char base[48];name(n,base,sizeof(base));const std::string path=std::string("/sd/logs/")+base;
        uint8_t previous[512];uint64_t previous_size=0;
        if(file.openRead(path)) {
            const bool owned=file.size(previous_size) && previous_size==512ull+32ull*counts[n] && file.read(previous,sizeof(previous)) &&
                validLogHeader(previous) && logGet(previous+16,8)==0x5354415400000000ull+counts[n];
            file.close();
            if(!owned) {Console.println("[STATS-BENCH-ERROR] existing fixture is not owned; left unchanged");ready=-1;return;}
            created[n]=true;continue;
        }
        if(!file.open(path,true)) {Console.println("[STATS-BENCH-ERROR] fixture exists or cannot create; never overwrite");ready=-1;return;}
        created[n]=true;
        uint8_t header[512];encodeLogHeader(header,0x5354415400000000ull+counts[n],1793000000000ull);
        bool ok=file.write(header,sizeof(header));uint8_t records[16][32];
        for(unsigned i=0;ok && i<counts[n];) {
            const unsigned count=std::min(16u,counts[n]-i);
            for(unsigned j=0;j<count;++j) {
                GpsFix fix{};fix.valid=true;fix.latitude=35.68+(i+j)%10000*0.000001;fix.longitude=140.02;
                fix.fields=GpsTime|GpsSpeed|GpsAltitude;fix.utc_epoch_ms=1793000000000ull+uint64_t(i+j)*1000;
                fix.speed_kmh=10+(i+j)%100;fix.altitude_m=10+(i+j)%1000*0.01;
                ok=ok && encodeLogRecord(fix,records[j]);
            }
            ok=ok && file.write(records[0],count*32);i+=count;
            if((i&1023u)==0)vTaskDelay(pdMS_TO_TICKS(1));
        }
        ok=file.close() && ok;
        if(!ok) {Console.println("[STATS-BENCH-ERROR] fixture write failed");ready=-1;return;}
    }
    Console.println("[STATS-BENCH] fixtures ready");ready=1;
}
inline void removeFixtures() {
    bool ok=true;
    for(unsigned n=0;n<3;++n)if(created[n]) {
        char base[48];name(n,base,sizeof(base));const std::string path=std::string("/sd/logs/")+base;
        if(std::remove(path.c_str()))ok=false;else created[n]=false;
    }
    if(record_name[0]) {
        const std::string path=std::string("/sd/logs/")+record_name;
        FILE* input=std::fopen(path.c_str(),"rb");uint8_t header[512];
        const bool owned=input && std::fread(header,1,sizeof(header),input)==sizeof(header) && validLogHeader(header) && logGet(header+16,8)==record_session;
        if(input)std::fclose(input);
        if(!owned || std::remove(path.c_str()))ok=false;
    }
    cleanup=ok?2:-1;
}
}
''')
p=app/'task/storage/GpsStorageTask.cpp';s=replace_once(p.read_text(),'#include "GpsStorageTask.h"','#include "GpsStorageTask.h"\n#include "../../benchmark/StatisticsBenchmark.h"')
s=replace_once(s,'        sendReturn();\n        notify();','''        sendReturn();
        notify();
        if(initialized_ && state_.card_state==CardState::Mounted && !hasJob() && !has_read_ && !state_.busy && taskTimeMs()>5000 && benchmark::ready==0)benchmark::fixtures(drive_);
        if(benchmark::cleanup==1 && !has_read_ && !replies_.completionPending())benchmark::removeFixtures();''')
s=replace_once(s,'        reader.step(&log_writer_);','        reader.step(&log_writer_);benchmark::sample();')
s=replace_once(s,'    result.logs=read;','''    result.logs=read;
    if(read.request.kind==LogReadKind::Statistics)Console.printf("[STATS-STORAGE] error=%u points=%lu stack_free=%u heap=%u largest=%u\\n",unsigned(read.error),(unsigned long)read.data.statistics.processed,unsigned(uxTaskGetStackHighWaterMark(nullptr)),ESP.getFreeHeap(),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));''')
p.write_text(s)
p=app/'task/storage/log/LogReader.cpp';s=replace_once(p.read_text(),'#include "LogReader.h"','#include "LogReader.h"\n#include "../../../benchmark/StatisticsBenchmark.h"')
s=replace_once(s,'            GpsFix fix{};const auto decode_start=monotonicMicros();','''            if(benchmark::mode==0) {++value.processed;continue;}
            GpsFix fix{};const auto decode_start=monotonicMicros();''')
s=replace_once(s,'            const auto math_start=monotonicMicros();statistics_.add(fix);','''            if(benchmark::mode==1) {++value.processed;continue;}
            const auto math_start=monotonicMicros();statistics_.add(fix);''');p.write_text(s)
(app/'benchmark/StatisticsBenchmark.inc').write_text(r'''
static unsigned bench_stage=0,bench_case=0;static uint32_t bench_time=0,bench_accepted=0;static bool bench_recording=false;
if(data_.boot.complete && screen_!=ScreenId::Boot && data_.settings.screen_timeout!=4) {
    data_.settings.screen_timeout=4;++data_.revision;ui_data_dirty_=true;
}
if(bench_stage==5 && data_.recording.state==RecordingState::Recording && data_.storage.log.accepted>=2) {bench_stage=0;bench_recording=true;}
if(bench_stage==6 && loggerStopped(data_.recording,data_.storage.log)) {
    std::snprintf(benchmark::record_name,sizeof(benchmark::record_name),"%s",data_.storage.log.name);benchmark::record_session=data_.storage.log.session_id;
    Console.printf("[STATS-RECORDING] accepted=%lu saved=%lu dropped=%lu error=%u\n",(unsigned long)data_.storage.log.accepted,(unsigned long)data_.storage.log.saved,(unsigned long)data_.recording.dropped,unsigned(data_.storage.log.error));
    benchmark::cleanup=1;bench_stage=3;
}
if(benchmark::ready==1 && bench_stage<3 && data_.boot.complete && screen_!=ScreenId::Boot && mode_==DisplayMode::View) {
    if(bench_stage==0 && !data_.storage.busy && !browse_waiting_ && !map_request_.token && !place_borrow_.token && !place_flow_.pending()) {
        if(bench_case==19) {
            setScreen(ScreenId::LivePlace);UiAction stop{};stop.action_id=0x7f000002;stop.log_action=LogAction::Stop;stop.confirmed=true;
            queueLogAction(stop);bench_stage=6;
        } else if(bench_case==18 && !bench_recording) {
            data_.settings.log_interval=0;data_.settings.flush_interval=0;UiAction start{};start.action_id=0x7f000001;start.log_action=LogAction::Start;start.confirmed=true;
            queueLogAction(start);bench_stage=5;
        }
        else {
            const unsigned size=std::min(2u,bench_case/6);benchmark::mode=bench_case==18?2:bench_case%3;
            setScreen(ScreenId::Logs);auto& view=data_.editBrowse();view.loading=false;view.count=1;view.selected=0;
            benchmark::name(size,view.names[0],sizeof(view.names[0]));screen_resources_.logs().need=false;++view.revision;
            UiAction open{};open.browse_action=BrowseAction::OpenFile;open.browse_revision=view.revision;open.browse_index=0;
            bench_time=taskTimeMs();operateBrowser(open);bench_stage=1;
        }
    } else if(bench_stage==1 && screen_==ScreenId::LogProperties && !data_.browse().loading && !browse_waiting_ && !data_.storage.busy) {
        Console.printf("[STATS-PROPERTY] n=%u wall_ms=%lu records=%lu error=%u\n",benchmark::counts[std::min(2u,bench_case/6)],(unsigned long)(taskTimeMs()-bench_time),(unsigned long)data_.browse().summary.records,unsigned(data_.browse().error));
        if(data_.browse().error!=LogReadError::None) {Console.println("[STATS-BENCH-DONE] PROPERTY ERROR");bench_stage=4;}
        else {
            bench_accepted=data_.storage.log.accepted;
            benchmark::min_heap=UINT32_MAX;benchmark::min_largest=UINT32_MAX;
            UiAction stats{};stats.browse_action=BrowseAction::Statistics;if(operateBrowser(stats)) {bench_time=taskTimeMs();bench_stage=2;}
        }
    } else if(bench_stage==2 && screen_==ScreenId::LogStatistics && !data_.browse().loading && !browse_waiting_) {
        const auto& view=data_.browse();const auto& v=view.statistics;
        Console.printf("[STATS-BENCH] n=%u mode=%d rep=%u error=%u points=%lu elapsed_ms=%lu wall_ms=%lu read_us=%llu decode_us=%llu math_us=%llu max_step_us=%lu heap=%lu largest=%lu app_stack=%u\n",
            benchmark::counts[std::min(2u,bench_case/6)],int(benchmark::mode),bench_case%6/3,unsigned(view.error),(unsigned long)v.processed,
            (unsigned long)v.elapsed_ms,(unsigned long)(taskTimeMs()-bench_time),(unsigned long long)v.read_us,(unsigned long long)v.decode_us,
            (unsigned long long)v.math_us,(unsigned long)v.max_step_us,(unsigned long)benchmark::min_heap.load(),
            (unsigned long)benchmark::min_largest.load(),unsigned(uxTaskGetStackHighWaterMark(nullptr)));
        if(bench_case==18)Console.printf("[STATS-WHILE-RECORDING] before=%lu after=%lu saved=%lu dropped=%lu\n",(unsigned long)bench_accepted,(unsigned long)data_.storage.log.accepted,(unsigned long)data_.storage.log.saved,(unsigned long)data_.recording.dropped);
        if(view.error!=LogReadError::None) {Console.println("[STATS-BENCH-DONE] SCAN ERROR");bench_stage=4;}
        else {setScreen(ScreenId::LivePlace);++bench_case;bench_stage=0;}
    }
}
if(bench_stage==3 && benchmark::cleanup==2) {Console.println("[STATS-BENCH-DONE] OK fixtures removed; NVS/user logs unchanged");bench_stage=4;}
''')
p=app/'GpsApplication.cpp';s=replace_once(p.read_text(),'#include "GpsApplication.h"','#include "GpsApplication.h"\n#include "benchmark/StatisticsBenchmark.h"')
s=replace_once(s,'void GpsApplication::runOnce() {\n    drainNotifications();','void GpsApplication::runOnce() {\n    drainNotifications();\n#include "benchmark/StatisticsBenchmark.inc"');p.write_text(s)
p=app/'task/ui/GpsUiModel.cpp';p.write_text(replace_once(p.read_text(),'const auto timeout = ScreenTimeouts[data_.settings.screen_timeout];','const auto timeout = 0u; // Diagnostic automation only; NVS is unchanged.'))
(r/'.pio/log-statistics-bench.ini').write_text(diagnostic_config(r,w))
print(w)

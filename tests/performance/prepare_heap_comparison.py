"""Pair the saved pre-statistics source and current source with identical heap sampling."""
from pathlib import Path
import argparse,shutil,sys
r=Path(__file__).resolve().parents[2];sys.path.insert(0,str(r/'tests/performance'))
from benchmark_support import replace_once,diagnostic_config
p=argparse.ArgumentParser();p.add_argument('version',choices=['before','after']);p.add_argument('--source',type=Path);a=p.parse_args()
source=(a.source/'application') if a.source else (r/'.pio/log-statistics-before/application' if a.version=='before' else r/'application')
w=r/'.pio'/('heap-fixed-codes-'+a.version if a.source else 'heap-statistics-'+a.version);w.mkdir(exist_ok=True);app=w/'application'
if app.exists():shutil.rmtree(app)
shutil.copytree(source,app);(app/'benchmark').mkdir()
for name in ['HeapComparisonProbe.h','HeapComparisonBenchmark.inc']:shutil.copy2(r/'tests/performance'/name,app/'benchmark'/name)
p=app/'Application.cpp';s=replace_once(p.read_text(),'#include "Application.h"','#include "Application.h"\n#include "benchmark/HeapComparisonProbe.h"\n#define HEAP_COMPARISON_AFTER '+str(int(a.version=='after'))+'\n#define HEAP_COMPARISON_HAS_STATS '+str(int('LogStatistics' in (source/'task/ui/UiTypes.h').read_text())))
s=replace_once(s,'    usb_mode_ = usb_mode;','    usb_mode_ = usb_mode;\n    if(!usb_mode_ && !benchmark::begin())return false;')
s=replace_once(s,'        if (!applyGpsSnapshot(data_.gps, event.payload.gps)) return;','        if (!applyGpsSnapshot(data_.gps, event.payload.gps)) return;\n        if(data_.gps.fix.valid){data_.gps.fix.latitude=35.681641;data_.gps.fix.longitude=140.027194;}')
s=replace_once(s,'void Application::runOnce() {\n    drainNotifications();','void Application::runOnce() {\n    drainNotifications();\n#include "benchmark/HeapComparisonBenchmark.inc"');p.write_text(s)
p=app/'task/ui/UiModel.cpp';p.write_text(replace_once(p.read_text(),'const auto timeout = ScreenTimeouts[data_.settings.screen_timeout];','const auto timeout = 0u; // Paired test only, no NVS writes.'))
# Old/new code-path probes use the same sampler and never own application buffers.
p=app/'utils/HeapDiagnostics.h';s=replace_once(p.read_text(),'#include "../Config.h"','#include "../Config.h"\n#include "../benchmark/HeapComparisonProbe.h"');s=replace_once(s,'    if (!config::MapHeapDiagnostics) return;','    if (!config::MapHeapDiagnostics) return;\n    benchmark::sample();');p.write_text(s)
# Prevent unrelated persistent selection changes if the bench encounters bad media.
p=app/'task/storage/StorageTask.cpp';s=replace_once(p.read_text(),'    if (!releaseUnusableDataset(settings_store_, state_, reason)) return;','    Console.printf("[HEAP-ERROR] dataset failure=%u; NVS unchanged\\n",unsigned(reason));\n    place_lookup_.close();return;');p.write_text(s)
(r/'.pio'/(w.name+'.ini')).write_text(diagnostic_config(r,w));print(w)

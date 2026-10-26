"""Prepare an isolated production-flow memory trial; never modify NVS or logs."""
from pathlib import Path
import shutil
from benchmark_support import prepare_workspace,diagnostic_config
root=Path(__file__).resolve().parents[2]
work=prepare_workspace(root,'map-session-bench',[])
shutil.copy2(root/'tests/performance/MapSessionBenchmark.inc',work/'application/benchmark/MapSessionBenchmark.inc')
p=work/'application/Application.cpp';s=p.read_text()
s=s.replace('#include "Application.h"','#include "Application.h"\n#include "utils/HeapDiagnostics.h"',1)
s=s.replace('void Application::runOnce() {\n    drainNotifications();','void Application::runOnce() {\n    drainNotifications();\n#include "benchmark/MapSessionBenchmark.inc"',1)
p.write_text(s)
p=work/'application/task/storage/StorageTask.cpp';s=p.read_text()
s=s.replace('    if (!releaseUnusableDataset(settings_store_, state_, reason)) return;',
    '    Console.printf("[SESSION-BENCH-ERROR] dataset reason=%u; NVS unchanged\\n",unsigned(reason));\n    place_lookup_.close();return;')
p.write_text(s)
(root/'.pio/map-session-bench.ini').write_text(diagnostic_config(root,work))
print('Prepared',work)

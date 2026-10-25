"""Compare RAM and disk chunk indexes on identical cached rectangles (host IO)."""
from pathlib import Path
import argparse
import json
import statistics
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def summarize(log):
    rows=[]
    for line in log.splitlines():
        if not line.startswith(('[DISK-INDEX-CASE]','[DISK-INDEX-INIT]')):
            continue
        row={'kind':'case' if line.startswith('[DISK-INDEX-CASE]') else 'init'}
        for word in line.split()[1:]:
            key,value=word.split('=',1)
            row[key]=int(value) if value.isdecimal() else value
        rows.append(row)
    assert len([r for r in rows if r['kind']=='case'])==18
    summary={}
    for mode in ('ram','disk'):
        initial=[r for r in rows if r['kind']=='init' and r['mode']==mode]
        summary[mode]={'init_us_median':statistics.median(r['init_us'] for r in initial),
                       'retained_heap_median':statistics.median(r['heap_before']-r['heap_after'] for r in initial)}
        for scenario in range(3):
            cases=[r for r in rows if r['kind']=='case' and r['mode']==mode and r['scenario']==scenario]
            summary[mode][str(scenario)]={key:statistics.median(r[key] for r in cases)
                for key in ('query_us','rect_us','hit_us','map_bytes','map_reads','map_seeks','index_bytes','index_reads','index_seeks')}
    for scenario in range(3):
        assert len({r['hash'] for r in rows if r['kind']=='case' and r['scenario']==scenario})==1
    return {'runs':rows,'median':summary}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('map',type=Path);parser.add_argument('book',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='rgp-index-benchmark-') as folder:
        binary=Path(folder)/'benchmark'
        subprocess.run(['g++','-O2','-DNDEBUG','-std=c++17','-DREVERSEGEOPUTER_DIAGNOSTICS=1','-Wall','-Wextra','-Werror','-pedantic',
            '-I'+str(ROOT/'application'),'-I'+str(ROOT/'third_party/galuchat/include'),
            '-I'+str(ROOT/'tests/performance'),str(ROOT/'tests/performance/index_benchmark_host.cpp'),
            str(ROOT/'tests/performance/baseline/RamCachedMapReader.cpp'),
            str(ROOT/'application/task/storage/galuchat/CachedMapReader.cpp'),
            str(ROOT/'application/task/storage/galuchat/DiskIndexedMapReader.cpp'),
            str(ROOT/'application/task/storage/dataset/DatasetIndex.cpp'),
            str(ROOT/'application/task/storage/dataset/DatasetMetadata.cpp'),
            str(ROOT/'application/task/storage/galuchat/TextEncoding.cpp'),
            str(ROOT/'application/Settings.cpp'),'-o',str(binary)],check=True,timeout=120)
        result=subprocess.run([str(binary),str(Path(folder)/'dataset'),str(args.map.resolve()),str(args.book.resolve())],
                              text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
        (args.output/'host.log').write_text(result.stdout)
        print(result.stdout,end='');result.check_returncode()
        summary=summarize(result.stdout);(args.output/'host-results.json').write_text(json.dumps(summary,indent=2)+'\n')
        print(json.dumps(summary['median'],indent=2))

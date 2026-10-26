"""Measure C++ allocation traffic for the three memory changes on identical input."""
import argparse,json,os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--before',required=True,type=Path)
p.add_argument('--output',required=True,type=Path)
p.add_argument('files',nargs='+',type=Path,help='map Wordbook [map Wordbook ...]')
a=p.parse_args()
if len(a.files)%2:p.error('Provide map/Wordbook pairs')
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
sources=['Settings.cpp','task/storage/dataset/DatasetIndex.cpp','task/storage/dataset/DatasetMetadata.cpp',
         'task/storage/galuchat/DiskIndexedWordBookReader.cpp','task/storage/galuchat/TextEncoding.cpp',
         'task/storage/log/LogReader.cpp','task/storage/log/LogWriter.cpp']
results={}
for label,project in [('old',a.before.resolve()),('new',root)]:
    binary=out/label
    subprocess.run(['g++','-std=c++17','-O1','-no-pie','-pthread','-Wall','-Wextra','-Werror','-pedantic',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-D_GLIBCXX_ASSERTIONS','-DREVERSEGEOPUTER_DIAGNOSTICS=1',
        '-I'+str(project/'application'),'-I'+str(root/'third_party/galuchat/include'),
        str(root/'tests/performance/memory_three_host.cpp'),*[str(project/'application'/s) for s in sources],
        '-o',str(binary)],check=True,timeout=300)
    results[label]=[]
    for n in range(0,len(a.files),2):
        work=out/(label+'-'+str(n//2))
        if work.exists():raise RuntimeError('Use a fresh output directory; previous measurements are preserved')
        run=subprocess.run([str(binary),str(work),str(a.files[n].resolve()),str(a.files[n+1].resolve())],
            text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True,timeout=180,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        (out/(label+'-'+str(n//2)+'.log')).write_text(run.stdout+run.stderr)
        results[label].append({v['case']:v for v in map(json.loads,run.stdout.splitlines())})
for n in range(len(a.files)//2):
    for case,old in results['old'][n].items():
        new=results['new'][n][case]
        if 'fingerprint' in old:assert old['fingerprint']==new['fingerprint'],(n,case)
        else:assert old==new,(n,case,'I/O changed')
(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
print('PASS: all result fingerprints and Wordbook I/O counts match')

"""Verify fixed caller-owned map buffers against an independent reference layout."""
import argparse,os,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('files',nargs='+',type=Path,help='map Wordbook [map Wordbook ...]')
a=p.parse_args()
if len(a.files)%2:p.error('Provide map/Wordbook pairs')
with tempfile.TemporaryDirectory(prefix='rgp-borrowed-map-') as folder:
    binary=Path(folder)/'map-tests'
    sources=['tests/borrowed_map_file_tests.cpp','application/Settings.cpp',
        'application/task/storage/dataset/DatasetIndex.cpp','application/task/storage/dataset/DatasetMetadata.cpp',
        'application/task/storage/galuchat/MapSource.cpp','application/task/storage/galuchat/DiskIndexedMapReader.cpp',
        'application/task/storage/galuchat/TextEncoding.cpp',
        'application/task/storage/galuchat/PlaceLookup.cpp','application/task/storage/galuchat/CachedMapReader.cpp',
        'application/task/storage/galuchat/DiskIndexedWordBookReader.cpp']
    subprocess.run(['g++','-std=c++17','-O1','-no-pie','-Wall','-Wextra','-Werror','-pedantic',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-D_GLIBCXX_ASSERTIONS',
        '-I'+str(root/'tests'),'-I'+str(root/'tests/support'),'-I'+str(root/'application'),
        '-I'+str(root/'third_party/galuchat/include'),*[str(root/s) for s in sources],'-o',str(binary)],check=True,timeout=300)
    for n in range(0,len(a.files),2):
        print(a.files[n].name,flush=True)
        subprocess.run([str(binary),str(Path(folder)/str(n//2)),str(a.files[n].resolve()),str(a.files[n+1].resolve())],
            check=True,timeout=180,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))

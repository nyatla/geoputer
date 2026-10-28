"""Compare ordinary and UI-workspace place queries against real UTF-8 datasets."""
import argparse,subprocess,tempfile,os
from pathlib import Path
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('files',type=Path,nargs='+');a=p.parse_args()
if len(a.files)%2:p.error('Provide map/Wordbook pairs')
with tempfile.TemporaryDirectory(prefix='rgp-borrowed-place-') as folder:
 binary=Path(folder)/'place-tests'
 sources=['tests/borrowed_place_file_tests.cpp','application/Settings.cpp',
 'application/task/storage/dataset/DatasetIndex.cpp','application/task/storage/dataset/DatasetMetadata.cpp',
 'application/task/storage/galuchat/PlaceLookup.cpp','application/task/storage/galuchat/CachedMapReader.cpp',
 'application/task/storage/galuchat/DiskIndexedMapReader.cpp','application/task/storage/galuchat/DiskIndexedWordBookReader.cpp',
 'application/task/storage/galuchat/TextEncoding.cpp']
 subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-no-pie',
 '-fsanitize=address,undefined','-fno-sanitize-recover=all','-D_GLIBCXX_ASSERTIONS',
 '-I'+str(r/'tests'),'-I'+str(r/'tests/support'),'-I'+str(r/'application'),'-I'+str(r/'third_party/galuchat/include'),
 *[str(r/s) for s in sources],'-o',str(binary)],check=True,timeout=300)
 for i in range(0,len(a.files),2):
  print(a.files[i].name,flush=True)
  subprocess.run([str(binary),str(Path(folder)/str(i//2)),str(a.files[i].resolve()),str(a.files[i+1].resolve())],check=True,timeout=180,env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0"))

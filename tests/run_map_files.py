"""Check real map rendering and isolation from the existing 128x128 cache."""
from pathlib import Path
import argparse
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('map',type=Path);p.add_argument('book',type=Path)
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='reversegeoputer-map-files-') as directory:
    binary=Path(directory)/'map-tests'
    sources=['tests/map_file_tests.cpp','application/Settings.cpp',
        'application/task/storage/dataset/DatasetIndex.cpp','application/task/storage/dataset/DatasetMetadata.cpp',
        'application/task/storage/galuchat/MapSource.cpp','application/task/storage/galuchat/PlaceLookup.cpp',
        'application/task/storage/galuchat/CachedMapReader.cpp','application/task/storage/galuchat/DiskIndexedMapReader.cpp',
        'application/task/storage/galuchat/DiskIndexedWordBookReader.cpp','application/task/storage/galuchat/TextEncoding.cpp']
    subprocess.run(['g++','-std=c++17','-O2','-g','-Wall','-Wextra','-Werror','-pedantic',
        '-fsanitize=undefined','-fno-sanitize-recover=all','-D_GLIBCXX_ASSERTIONS',
        '-I',str(root/'tests'),'-I',str(root/'tests/support'),'-I',str(root/'application'),'-I',str(root/'third_party/galuchat/include'),
        *[str(root/s) for s in sources],'-o',str(binary)],check=True,timeout=180)
    subprocess.run([str(binary),str(Path(directory)/'data'),str(a.map.resolve()),str(a.book.resolve())],check=True,timeout=180)

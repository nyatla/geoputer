"""Assert that INI execution/display parsing has no C++ heap allocations."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rgp-ini-heap-') as folder:
    binary=Path(folder)/'metadata-tests'
    subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-no-pie',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-D_GLIBCXX_ASSERTIONS',
        '-I'+str(r/'tests/support'),'-I'+str(r/'application'),
        str(r/'tests/metadata_heap_tests.cpp'),str(r/'application/Settings.cpp'),
        str(r/'application/task/storage/dataset/DatasetMetadata.cpp'),
        str(r/'application/task/storage/galuchat/TextEncoding.cpp'),'-o',str(binary)],check=True,timeout=90)
    subprocess.run([str(binary)],check=True,timeout=15)

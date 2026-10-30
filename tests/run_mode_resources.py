"""Check real boot-mode resource ownership and union lifetime on the host."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rgp-mode-resources-') as d:
    binary=Path(d)/'modes'
    sources=['tests/mode_resources_tests.cpp','application/task/storage/log/LogWriter.cpp','application/task/storage/log/LogReader.cpp','application/task/storage/export/LogExporter.cpp']
    subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-fno-access-control','-fsanitize=undefined','-fno-sanitize-recover=all','-pthread',
        *['-I'+str(r/p) for p in ['tests/support/map-app','tests/support/storage-task','tests/support/usb','tests/support','application','third_party/galuchat/include']],
        *[str(r/p) for p in sources],'-o',str(binary)],check=True,timeout=120)
    subprocess.run([str(binary)],check=True,timeout=15)

"""Exercise the real App map flow with queued Storage and UI peers on the host."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='geoputer-map-scale-') as d:
    binary=Path(d)/'map-scale'
    sources=['tests/map_scale_app_tests.cpp','application/MapView.cpp',
             *[str(p.relative_to(r)) for p in sorted((r/'application/task/ui').glob('GpsUiModel*.cpp'))],
             'application/task/ui/SettingsText.cpp','application/task/ui/LicenseText.cpp','application/Settings.cpp',
             'application/task/storage/log/LogWriter.cpp','application/task/storage/log/LogReader.cpp',
             'application/task/storage/export/LogExporter.cpp']
    subprocess.run(['g++','-O1','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-fno-access-control',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-D_GLIBCXX_ASSERTIONS','-pthread','-I'+str(r/'tests/support/gps-control'),'-I'+str(r/'tests/support/map-app'),'-I'+str(r/'tests/support/storage-task'),
        '-I'+str(r/'tests/support/usb'),'-I'+str(r/'tests/support'),'-I'+str(r/'application'),
        '-I'+str(r/'third_party/galuchat/include'),*[str(r/p) for p in sources],'-o',str(binary)],check=True,timeout=120)
    subprocess.run([str(binary)],check=True,timeout=15)

"""Exercise the production Application logging and UI/GPS controls with queued host peers."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='reversegeoputer-logger-app-') as d:
    sources=['tests/logger_app_tests.cpp','application/Application.cpp','application/DatasetBrowser.cpp','application/LogBrowser.cpp','application/Alarm.cpp','application/MapView.cpp','application/task/gps/GpsTask.cpp',
        'application/task/gps/GpsReceiver.cpp','application/task/gps/NmeaParser.cpp','application/task/ui/UiModel.cpp',
        'application/task/ui/SettingsText.cpp','application/task/ui/LicenseText.cpp','application/Settings.cpp',
        'application/task/storage/log/LogWriter.cpp','application/task/storage/log/LogReader.cpp','application/task/storage/export/LogExporter.cpp']
    binary=Path(d)/'logger-app'
    subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-fno-access-control',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-fsanitize=undefined','-fno-sanitize-recover=all',
        '-D_GLIBCXX_ASSERTIONS','-pthread',
        *['-I'+str(r/p) for p in ['tests/support/gps-control','tests/support/storage-task','tests/support/usb','tests/support','application','third_party/galuchat/include']],
        *[str(r/p) for p in sources],'-o',str(binary)],check=True,timeout=180)
    subprocess.run([str(binary)],check=True,timeout=15)

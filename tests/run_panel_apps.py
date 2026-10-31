"""Test the concrete USB and Wi-Fi application coordinators with queued peers."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rgp-panel-apps-') as folder:
 binary=Path(folder)/'apps'
 subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-fno-access-control',
  '-fsanitize=undefined','-fno-sanitize-recover=all','-pthread',
  *['-I'+str(r/p) for p in ['tests/support/gps-control','tests/support/storage-task','tests/support/usb','tests/support','application']],
  *[str(r/p) for p in ['tests/panel_app_tests.cpp','application/apps/usb/UsbApplication.cpp','application/apps/wifi/WifiApplication.cpp',
   'application/task/storage/log/LogReader.cpp','application/task/storage/log/LogWriter.cpp','application/task/storage/export/LogExporter.cpp']],'-o',str(binary)],check=True,timeout=120)
 subprocess.run([str(binary)],check=True,timeout=20)

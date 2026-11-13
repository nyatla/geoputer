"""Run production GPS task control code with UART and clock adapters."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='geoputer-gps-control-') as d:
    binary=Path(d)/'gps-control'
    subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-fno-access-control',
        '-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(r/'tests/support/gps-control'),
        '-I'+str(r/'tests/support'),'-I'+str(r/'application'),
        *[str(r/p) for p in ['tests/gps_control_tests.cpp','application/task/gps/GpsTask.cpp',
           'application/task/gps/GpsReceiver.cpp','application/task/gps/NmeaParser.cpp']],'-o',str(binary)],check=True,timeout=120)
    subprocess.run([str(binary)],check=True,timeout=15)

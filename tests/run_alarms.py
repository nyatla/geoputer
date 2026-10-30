"""Run production alarm engine, persistence and UI contracts on the host."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='reversegeoputer-alarms-') as folder:
    work=Path(folder);exe=work/'alarms'
    sources=['tests/alarm_tests.cpp','application/task/storage/alarm/AlarmFile.cpp','application/Settings.cpp','application/task/ui/UiModel.cpp','application/task/ui/SettingsText.cpp','application/task/ui/LicenseText.cpp']
    subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie','-pthread','-I'+str(r/'tests/support'),'-I'+str(r/'application'),*map(lambda p:str(r/p),sources),'-o',str(exe)],check=True,timeout=180)
    subprocess.run([str(exe),str(work/'dataset')],check=True,timeout=20)

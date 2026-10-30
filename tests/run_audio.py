"""Exercise production audio scheduling and ES8311 power writes with driver mocks."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rgp-audio-') as folder:
    exe=Path(folder)/'audio'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=undefined','-fno-sanitize-recover=all',
        *['-I'+str(r/p) for p in ['tests/support/audio','tests/support/gps-control','application']],
        str(r/'tests/audio_tests.cpp'),str(r/'application/hardware/AudioOutput.cpp'),str(r/'application/power/AudioPower.cpp'),'-o',str(exe)],check=True,timeout=60)
    subprocess.run([str(exe)],check=True,timeout=10)

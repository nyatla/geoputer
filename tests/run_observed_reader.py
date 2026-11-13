"""Exercise both the host stdio and ESP32 FatFs Reader paths with UBSan."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='geoputer-reader-') as folder:
    for diagnostics in (0,1):
        for backend in ['stdio','fatfs']:
            binary=Path(folder)/(backend+str(diagnostics))
            options=[]
            if backend=='fatfs':
                options=['-DARDUINO_ARCH_ESP32','-DGEOPUTER_TEST_FATFS','-I',str(root/'tests/support/reader')]
            subprocess.run(['g++','-O2','-std=c++17',f'-DGEOPUTER_DIAGNOSTICS={diagnostics}','-Wall','-Wextra','-Werror','-pedantic',
                '-fsanitize=undefined','-fno-sanitize-recover=all','-D_GLIBCXX_ASSERTIONS',*options,
                '-I',str(root),'-I',str(root/'third_party/galuchat/include'),
                str(root/'tests/observed_file_reader_test.cpp'),'-o',str(binary)],check=True,timeout=90)
            subprocess.run([str(binary)],check=True,timeout=30)
            print(backend,'diagnostics=',diagnostics,'Reader: PASS',flush=True)

"""Check borrowed-buffer lifetime and I/O equivalence with both host backends."""
import os
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rgp-borrowed-tests-') as directory:
    for diagnostics in (0,1):
        for backend in ('stdio','fatfs'):
            binary=Path(directory)/(backend+str(diagnostics))
            options=[]
            sources=['tests/borrowed_reader_tests.cpp']
            if backend=='fatfs':
                options=['-DARDUINO_ARCH_ESP32','-DGEOPUTER_TEST_FATFS','-I'+str(root/'tests/support/reader')]
            else:
                sources+=['application/task/storage/log/LogReader.cpp','application/task/storage/log/LogWriter.cpp']
            subprocess.run(['g++','-std=c++17','-O1','-no-pie','-pthread','-Wall','-Wextra','-Werror','-pedantic',
                '-fsanitize=address,undefined','-fno-sanitize-recover=all','-D_GLIBCXX_ASSERTIONS',
                f'-DREVERSEGEOPUTER_DIAGNOSTICS={diagnostics}',*options,'-I'+str(root),'-I'+str(root/'application'),
                '-I'+str(root/'third_party/galuchat/include'),*[str(root/s) for s in sources],'-o',str(binary)],check=True,timeout=120)
            # LeakSanitizer cannot run in the traced host environment. The test
            # checks borrowed state and fake FatFs handle counts explicitly.
            subprocess.run([str(binary)],check=True,timeout=30,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
            print('PASS:',backend,'diagnostics',diagnostics,flush=True)

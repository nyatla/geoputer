"""Verify Storage job switching, close failures and allocation-free fixed images."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rgp-storage-work-test-') as folder:
 binary=Path(folder)/'work-tests'
 cmd=['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-fno-access-control',
      '-fsanitize=undefined','-fno-sanitize-recover=all','-D_GLIBCXX_ASSERTIONS','-pthread',
      '-I'+str(r/'tests/support'),'-I'+str(r/'application'),
      str(r/'tests/storage_work_tests.cpp'),str(r/'application/task/storage/log/LogReader.cpp'),
      str(r/'application/task/storage/log/LogWriter.cpp'),'-Wl,--wrap=fclose','-o',str(binary)]
 subprocess.run(cmd,check=True,timeout=120);subprocess.run([str(binary)],check=True,timeout=15)

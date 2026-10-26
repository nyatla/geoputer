"""Verify allocation-free fixed headers against the existing SDK semantics."""
from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rgp-fixed-header-') as directory:
    binary=Path(directory)/'headers'
    subprocess.run(['g++','-std=c++17','-O1','-no-pie','-Wall','-Wextra','-Werror','-pedantic',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-D_GLIBCXX_ASSERTIONS',
        '-I'+str(r/'application'),'-I'+str(r/'third_party/galuchat/include'),str(r/'tests/fixed_header_tests.cpp'),'-o',str(binary)],check=True,timeout=120)
    subprocess.run([str(binary)],check=True,timeout=15,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))

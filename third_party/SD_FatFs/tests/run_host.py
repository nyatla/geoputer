#!/usr/bin/env python3
"""Exercise both independent FatFs copies in one process, with ASan/UBSan."""
from pathlib import Path
import os, shutil, subprocess, tempfile, re
ROOT=Path(__file__).resolve().parents[1]
def run(*cmd,**kwargs): subprocess.run(list(map(str,cmd)),check=True,timeout=60,**kwargs)
run('python3',ROOT/'tools/generate.py','--check')
with tempfile.TemporaryDirectory(prefix='sd-fatfs-test-') as temp:
    build=Path(temp); original=build/'standard';original.mkdir()
    up=ROOT/'upstream/esp-idf-v4.4.7'
    for name in ['ff.h','ff.c','diskio.h','ffunicode.c']:shutil.copyfile(up/name,original/name)
    # Only the host OS glue/configuration is substituted. The original filesystem
    # and Unicode conversion implementations remain unchanged.
    config=(ROOT/'tools/ffconf.h.in').read_text().replace('SD_FATFS_CONFIG_INCLUDED','TEST_STANDARD_CONFIG_INCLUDED')
    config=config.replace('#define FF_MAX_SS 512','#define FF_MAX_SS 4096')
    (original/'ffconf.h').write_text(config)
    options=['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie']
    objects=[]
    for prefix,folder,files in [('sd',ROOT/'src/sd_fatfs',['ff.c','ffunicode.c','device.c']),('standard',original,['ff.c','ffunicode.c'])]:
        for name in files:
            obj=build/(prefix+'-'+name+'.o');objects.append(obj)
            run('gcc','-std=c99',*options,'-Wall','-Wextra','-I'+str(folder),'-c',folder/name,'-o',obj)
    # No unprefixed FatFs/disk API may escape the dedicated objects.
    symbols=subprocess.check_output(['nm','-g','--defined-only',*map(str,objects[:3])],text=True)
    for symbol in re.findall(r'\b[TRDB]\s+(\w+)',symbols):
        if not symbol.startswith(('sd_fatfs_','SD_FATFS_','__odr_asan')):
            raise SystemExit('Non-isolated symbol: '+symbol)
    # Inclusion order must work in C as well as C++.
    for order in [False,True]:
        check=build/('headers'+str(order)+'.c')
        check.write_text(('#include <SD_FatFs.h>\n#include "ff.h"\n#include "diskio.h"\n' if order else '#include "ff.h"\n#include "diskio.h"\n#include <SD_FatFs.h>\n')+
                         '_Static_assert(FF_MAX_SS==4096,"SDK config");\n_Static_assert(SD_FATFS_FF_MAX_SS==512,"SD config");\n')
        run('gcc','-std=c11','-Werror','-I'+str(original),'-I'+str(ROOT/'src'),'-c',check,'-o',build/'headers.o')
        binary=build/('coexist-'+str(order))
        run('g++','-std=c++17',*options,'-no-pie',*(['-DSD_FIRST'] if order else []),'-I'+str(original),'-I'+str(ROOT/'src'),ROOT/'tests/coexist.cpp',*objects,'-pthread','-o',binary)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:handle_segv=0')
        run(binary,env=env)
print('Both header orders, isolated symbols, FAT32 + UTF-8 I/O and error paths: PASS')

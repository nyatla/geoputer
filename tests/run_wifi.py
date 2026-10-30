"""Exercise production Wi-Fi file operations on an actual in-memory FAT32 volume."""
from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rgp-wifi-') as d:
 d=Path(d)
 # Host aliases select the same independent FatFs used by Storage on the ESP32.
 s='#pragma once\n#include <SD_FatFs.h>\n'
 for n in ('FIL','FILINFO','FATFS','FRESULT','FF_DIR','FSIZE_t','DWORD','UINT'):s+=f'using {n}=SD_FATFS_{n};\n'
 for n in ('FR_OK','FR_NO_FILE','FR_NO_PATH','FR_INVALID_NAME','FR_EXIST','FR_DISK_ERR','FR_DENIED','AM_DIR','FA_READ','FA_WRITE','FA_CREATE_NEW'):s+=f'inline constexpr auto {n}=SD_FATFS_{n};\n'
 for n in ('stat','open','close','read','write','sync','getfree','mkdir','unlink','rename','opendir','readdir','closedir'):s+=f'inline constexpr auto f_{n}=sd_fatfs_f_{n};\n'
 s+='#define f_size(fp) ((fp)->obj.objsize)\n'
 (d/'ff.h').write_text(s)
 folder=r/'third_party/SD_FatFs/src'
 flags=['-g','-O1','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-pie']
 objects=[]
 for name in ('ff.c','ffunicode.c','device.c'):
  obj=d/(name+'.o');objects.append(str(obj))
  subprocess.run(['gcc','-std=c99',*flags,'-I'+str(folder/'sd_fatfs'),'-c',str(folder/'sd_fatfs'/name),'-o',str(obj)],check=True,timeout=60)
 binary=d/'files'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-fno-access-control',*flags,'-no-pie',
  '-I'+str(d),'-I'+str(folder),'-I'+str(r/'application'),str(r/'tests/wifi_files_tests.cpp'),str(r/'application/wifi/WifiFiles.cpp'),*objects,'-o',str(binary)],check=True,timeout=90)
 subprocess.run([str(binary)],check=True,timeout=20,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))

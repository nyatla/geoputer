#!/usr/bin/env python3
# Generate isolated C identifiers from pinned, unchanged ESP-IDF FatFs sources.
from pathlib import Path
import argparse, hashlib, json, re
ROOT = Path(__file__).resolve().parents[1]
UP = ROOT / 'upstream' / 'esp-idf-v4.4.7'
TOKEN = re.compile(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_][A-Za-z_0-9]*')

def outputs():
    records = json.loads((ROOT/'upstream/sources.json').read_text())
    for name, record in records.items():
        if hashlib.sha256((UP/name).read_bytes()).hexdigest() != record['sha256']:
            raise SystemExit('Upstream source hash mismatch: '+name)
    config = (ROOT/'tools/ffconf.h.in').read_text()
    texts = {name:(UP/name).read_text() for name in ['ff.h', 'diskio.h', 'ff.c', 'ffunicode.c']}
    # C99 fixed-width types avoid relying on the global Windows BYTE/DWORD types.
    start = texts['ff.h'].index('#if defined(_WIN32)')
    end = texts['ff.h'].index('/* Definitions of volume management */', start)
    texts['ff.h'] = texts['ff.h'][:start]+'''#define FF_INTDEF 2
#include <stdint.h>
typedef unsigned int UINT;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint16_t WCHAR;
typedef uint32_t DWORD;
typedef uint64_t QWORD;

'''+texts['ff.h'][end:]
    names = set('UINT BYTE WORD WCHAR DWORD QWORD TCHAR FSIZE_t FATFS FFOBJID FIL DIR FILINFO FRESULT PARTITION DSTATUS DRESULT VolToPart VolumeStr EOF'.split())
    for text in [texts['ff.h'], texts['diskio.h'], config]:
        names.update(re.findall(r'^\s*#\s*define\s+([A-Za-z_]\w*)', text, re.M))
        names.update(re.findall(r'\b(?:FR|RES)_[A-Z_]+\b', text))
        names.update(re.findall(r'\bFF_\w+\b', text))
    # Do not rename compiler/runtime macros or our already isolated header guard.
    names = {n for n in names if not n.startswith(('__','SD_FATFS_'))}
    def token(m):
        n = m.group()
        if re.fullmatch(r'(?:f_|ff_|disk_)\w+|get_fattime', n):
            return 'sd_fatfs_'+n
        return 'SD_FATFS_'+n.lstrip('_') if n in names else n
    result = {name:TOKEN.sub(token, text) for name,text in texts.items()}
    result['ffconf.h'] = TOKEN.sub(token,config)
    # The optional ESP32 VFS uses POSIX DIR, and its own malloc/free lifetime.
    # Core FatFs remains heap-free; do not route VFS allocation through LFN hooks.
    vfs = (UP/'vfs_fat.c').read_text().replace('#include "diskio_impl.h"', '#include "esp32_vfs.h"')
    vfs = vfs.replace('ff_memalloc', 'malloc').replace('ff_memfree', 'free')
    def vfs_token(match):
        name = match.group()
        if name == 'DIR': return name
        if name == 'esp_vfs_fat_register': return 'sd_fatfs_vfs_register'
        if name == 'esp_vfs_fat_unregister_path': return 'sd_fatfs_vfs_unregister_path'
        return token(match)
    result['esp32_vfs.c'] = '#ifdef ESP_PLATFORM\n'+TOKEN.sub(vfs_token,vfs)+'\n#endif /* ESP_PLATFORM */\n'
    return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');args=parser.parse_args()
    for name, text in outputs().items():
        dest=ROOT/'src/sd_fatfs'/name
        if args.check:
            if not dest.exists() or dest.read_text()!=text:
                raise SystemExit('Generated file is stale: '+str(dest))
        else:
            dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(text)
    print('SD_FatFs generated sources: '+('verified' if args.check else 'updated'))
if __name__=='__main__': main()

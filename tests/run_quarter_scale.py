"""Verify the striped quarter-scale prototype against an unstriped oracle."""
from pathlib import Path
import argparse,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('map',type=Path,nargs='?');p.add_argument('wordbook',type=Path,nargs='?');a=p.parse_args()
if bool(a.map)!=bool(a.wordbook):p.error('Specify both map and Wordbook')
with tempfile.TemporaryDirectory(prefix='quarter-scale-') as folder:
 work=Path(folder);exe=work/'test'
 sources=['tests/quarter_scale_tests.cpp','application/task/storage/dataset/DatasetIndex.cpp','application/task/storage/dataset/DatasetMetadata.cpp','application/task/storage/galuchat/DiskIndexedMapReader.cpp','application/task/storage/galuchat/TextEncoding.cpp','application/Settings.cpp']
 subprocess.run(['g++','-std=c++17','-O2','-g','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=undefined','-fno-sanitize-recover=all','-D_GLIBCXX_ASSERTIONS','-I',str(root/'tests'),'-I',str(root/'tests/support'),'-I',str(root/'application'),'-I',str(root/'third_party/galuchat/include'),*[str(root/s) for s in sources],'-o',str(exe)],check=True,timeout=180)
 args=[str(exe)]
 if a.map:args += [str(work/'data'),str(a.map.resolve()),str(a.wordbook.resolve())]
 subprocess.run(args,check=True,timeout=180)

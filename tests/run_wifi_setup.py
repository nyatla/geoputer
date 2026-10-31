"""Run Wi-Fi credential/QR/portal contracts with the actual M5GFX encoder."""
from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[1]
lib=r/'.pio/libdeps/cardputer-adv/M5GFX@0.2.33/src'
if not lib.exists():lib=r/'.pio/libdeps/cardputer-adv/M5GFX/src'
if os.environ.get("RGP_M5GFX_DIR"):lib=Path(os.environ["RGP_M5GFX_DIR"])/"src"
with tempfile.TemporaryDirectory(prefix='rgp-wifi-setup-') as folder:
 d=Path(folder);obj=d/'qrcode.o';binary=d/'setup'
 flags=['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie']
 subprocess.run(['gcc','-std=c99',*flags,'-c',str(lib/'lgfx/utility/lgfx_qrcode.c'),'-o',str(obj)],check=True,timeout=60)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic',*flags,'-no-pie',
    '-I'+str(r/'application'),'-I'+str(r/'tests/support'),'-I'+str(lib),str(r/'tests/wifi_setup_tests.cpp'),
    str(r/'application/wifi/WifiCredentials.cpp'),str(r/'application/wifi/WifiQrCode.cpp'),str(obj),'-o',str(binary)],check=True,timeout=90)
 subprocess.run([str(binary),str(d/'network.pgm'),str(d/'url.pgm')],check=True,timeout=20,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
 try:
  import zxingcpp
  from PIL import Image
 except ImportError:
  print('Independent QR decoding skipped (optional zxing-cpp/Pillow not installed)')
 else:
  for name,expected in [('network','WIFI:T:WPA;S:ReverseGeoputer-ABCD;P:ABCD2345EFGH6789;;'),('url','http://192.168.4.1/')]:
   result=zxingcpp.read_barcode(Image.open(d/(name+'.pgm')))
   assert result and result.text==expected,(name,result)
  print('PASS independent decoder: both native 100x100 QR images match network credentials / URL')

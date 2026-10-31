"""Render production screens and verify the common SD access overlay."""
from pathlib import Path
import argparse,re,subprocess,sys,tempfile
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools'))
from ui_preview_support import ADAPTER,M5GFX
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path);a=p.parse_args()
sources=[r/'application/task/ui'/f'{n}.cpp' for n in ('PixelFont','GpsUiRenderer','GpsUiModel','LicenseText','SettingsText','Shinonome')]
sources.append(r/'application/apps/common/PanelRenderer.cpp')
text=''.join(s.read_text() for s in sources)+(r/'application/task/ui/UiCanvas.h').read_text()+(r/'tests/status_icon_render_tests.cpp').read_text()
enums=(M5GFX/'v1/misc/enum.hpp').read_text();colors=[]
for n in sorted(set(re.findall(r'\bTFT_[A-Z]+\b',text))):
 value=re.search(rf'\b{n}\s*=\s*(0x[0-9A-Fa-f]+)',enums)[1];colors.append(f'inline constexpr uint16_t {n}={value};')
with tempfile.TemporaryDirectory(prefix='rgp-status-icons-') as folder:
 work=Path(folder);(work/'M5GFX.h').write_text(ADAPTER+'\n'+'\n'.join(colors));exe=work/'render'
 subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-fno-access-control','-fsanitize=undefined','-fno-sanitize-recover=all',
 '-I'+str(work),'-I'+str(r/'application'),'-I'+str(r/'tests/support'),'-I'+str(M5GFX/'Fonts'),str(r/'tests/status_icon_render_tests.cpp'),*map(str,sources),str(r/'application/Settings.cpp'),'-o',str(exe)],check=True,timeout=180)
 subprocess.run([str(exe)],check=True,timeout=60)
 if a.output:
  from PIL import Image
  import io
  a.output.mkdir(parents=True,exist_ok=True)
  for screen in ('live','settings','usb'):
   for active in (0,1):
    picture=Image.open(io.BytesIO(subprocess.check_output([str(exe),screen,str(active)]))).convert('RGB')
    stem=f'{screen}-sd-'+('active' if active else 'idle');picture.save(a.output/(stem+'.png'))
    picture.resize((960,540),getattr(Image,"Resampling",Image).NEAREST).save(a.output/(stem+'-preview.png'))

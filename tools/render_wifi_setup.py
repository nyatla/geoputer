"""Render the real Wi-Fi setup pages at native Cardputer resolution."""
import argparse,re,subprocess,tempfile
from pathlib import Path
from PIL import Image
from ui_preview_support import ROOT,M5GFX,ADAPTER
SCENE=r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "task/ui/UiRenderer.h"
using namespace geoputer;
int main(int argc,char** argv) {
 if(argc!=3)return 1;
 const unsigned page=std::atoi(argv[1]);const bool ja=std::strcmp(argv[2],"en")!=0;
 UiData data{};data.revision=1;data.settings=defaultSettings();data.settings.language=ja?UiLanguage::Japanese:UiLanguage::English;
 data.storage.settings_state=SettingsState::Loaded;data.storage.card_state=CardState::Mounted;data.storage.wifi.phase=WifiPhase::Ready;data.storage.wifi.portal=true;
 auto& w=data.storage.wifi;std::strcpy(w.ssid,"ReverseGeoputer-ABCD");std::strcpy(w.password,"ABCD2345EFGH6789");std::strcpy(w.url,"http://192.168.4.1/");
 UiModel model;model.setScreen({ScreenId::WifiManagement,DisplayMode::View,1,data,0,ActionResult::None});model.setBattery(72,false,true);
 for(unsigned i=0;i<(page<3?page:0);++i)model.keyPressed(UiKey::Right,1);
 if(page==3)model.keyPressed(UiKey::Recalculate,2);
 wifi::QrCode qr;qr.update(model.data().storage.wifi,model.wifiPage());
 M5GFX display;UiRenderer renderer(display,&qr);UiFrameBuffer frame;
 if(!renderer.begin(4,frame.data,sizeof(frame.data)))return 2;
 renderer.draw(model);std::printf("P6\n240 135\n255\n");
 for(auto pixel:display.pixels){std::fputc(((pixel>>11)&31)*255/31,stdout);std::fputc(((pixel>>5)&63)*255/63,stdout);std::fputc((pixel&31)*255/31,stdout);}
}
'''
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
 enum=(M5GFX/'v1/misc/enum.hpp').read_text();renderer=(ROOT/'application/task/ui/UiRenderer.cpp').read_text()+(ROOT/'application/task/ui/UiCanvas.h').read_text()
 colors=[]
 for name in sorted(set(re.findall(r'\bTFT_[A-Z]+\b',SCENE+renderer))):
  value=re.search(rf'\b{name}\s*=\s*(0x[0-9A-Fa-f]+)',enum)[1];colors.append(f'inline constexpr uint16_t {name}={value};')
 with tempfile.TemporaryDirectory(prefix='rgp-wifi-render-') as folder:
  d=Path(folder);(d/'M5GFX.h').write_text(ADAPTER+'\n'+'\n'.join(colors));(d/'scene.cpp').write_text(SCENE);obj=d/'qrcode.o'
  subprocess.run(['gcc','-std=c99','-O1','-c',str(M5GFX/'utility/lgfx_qrcode.c'),'-o',str(obj)],check=True,timeout=60)
  sources=[ROOT/'application/task/ui'/f'{name}.cpp' for name in ['PixelFont','UiRenderer','UsbRenderer','UiModel','LicenseText','SettingsText','Shinonome']]
  subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-I'+str(d),'-I'+str(ROOT/'application'),'-I'+str(ROOT/'tests/support'),'-I'+str(M5GFX/'Fonts'),'-I'+str(M5GFX.parent),str(d/'scene.cpp'),*map(str,sources),str(ROOT/'application/Settings.cpp'),str(ROOT/'application/wifi/WifiQrCode.cpp'),str(obj),'-o',str(d/'render')],check=True,timeout=180)
  for lang in ('ja','en'):
   for page,name in enumerate(('connect','web','text','renew')):
    ppm=d/'screen.ppm';ppm.write_bytes(subprocess.check_output([str(d/'render'),str(page),lang],timeout=15))
    image=Image.open(ppm).convert('RGB');assert image.size==(240,135);image.save(args.output/f'wifi-{lang}-{name}.png')
    image.resize((960,540),getattr(Image,'Resampling',Image).NEAREST).save(args.output/f'wifi-{lang}-{name}-preview.png')
  print('PASS native Wi-Fi setup renders: QR / URL / text / password confirmation, Japanese and English, 240x135')
if __name__=='__main__':main()

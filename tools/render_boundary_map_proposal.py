"""Render a 240x135 boundary-map proposal with native UI fonts and real estat regions.
This host-only mockup does not add a firmware screen or a 4-key binding.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile
from PIL import Image
from ui_preview_support import ROOT, M5GFX, ADAPTER

SCENE = r'''
#include <cstdio>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <galuchat/std_file_reader.hpp>
#include <galuchat/wgsmapset3_indexed_reader.hpp>
#include <galuchat/gis_wordbook_reader.hpp>
#include "task/ui/UiRenderer.h"
using namespace geoputer;
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    constexpr double latitude=35.681641,longitude=140.027194;
    galuchat::GaluchatWGSMapSet3IndexedReader map(std::make_shared<galuchat::FileReaderFactory>(argv[1]));
    const int cx=std::lround(longitude*map.unit_inv_x),cy=std::lround(latitude*map.unit_inv_y);
    constexpr int W=240,H=101,Y=18,MidX=120,MidY=68;
    const double meters_x=111320*std::cos(latitude*3.141592653589793/180)/map.unit_inv_x;
    const double meters_y=111320.0/map.unit_inv_y;
    const int source_h=2*int(std::ceil((H/2)*meters_x/meters_y))+1;
    auto raster=map.readWgsRectWithoutTail({cx-W/2,cy-source_h/2,W,source_h});
    const auto center=raster.get(W/2,source_h/2);
    if(!center)throw std::runtime_error("current point outside dataset");
    galuchat::GaluchatGisWordBookReader book(std::make_shared<galuchat::FileReaderFactory>(argv[2]));
    std::vector<std::string> names;
    if(!book.readStringSetByCode(center,names))throw std::runtime_error("region name not found");
    std::string name;
    for(size_t i=2;i<names.size();++i)name+=names[i];
    if(name.empty())name=names.back();
    std::fprintf(stderr,"region=%lld name=%s meters_per_pixel=%.3f\n",static_cast<long long>(center),name.c_str(),meters_x);
    M5GFX display;UiRenderer ui(display);if(!ui.begin())return 1;
    UiModel model;UiData data{};data.settings=defaultSettings();data.recording.state=RecordingState::Paused;
    data.storage.card_state=CardState::Mounted;data.storage.rg={1,1,RgCacheState::Ready};
    data.gps.fix.valid=true;data.gps.fix.utc_ms=19921000;data.gps.reception=GpsReception::Fix;
    model.setScreen({ScreenId::LivePlace,DisplayMode::View,1,data,0,ActionResult::None});model.setBattery(72,false,true);ui.draw(model);
    const auto original=display.pixels;
    UiCanvas canvas(&display);if(!canvas.begin(4))return 1;canvas.fillSprite(TFT_BLACK);
    PixelFont font(display);if(!font.begin())return 1;
    auto text=[&](int x,int y,const char* s,uint16_t color=TFT_LIGHTGREY){font.draw(canvas,x,y,s,TextRole::UI,color);};
    auto code=[&](int x,int y){
        const int gy=int(std::lround((MidY-y)*meters_x/meters_y))+source_h/2;
        return raster.get(x,gy);
    };
    // Neighbor-code differences produce one-pixel boundaries; region interiors remain black.
    for(int y=Y;y<Y+H;++y)for(int x=0;x<W;++x) {
        const auto a=code(x,y);
        const auto b=x+1<W?code(x+1,y):a;
        const auto c=y+1<Y+H?code(x,y+1):a;
        if(a!=b || a!=c)canvas.drawPixel(x,y,(a==center || b==center || c==center)?TFT_WHITE:TFT_DARKGREY);
    }
    canvas.fillRect(2,20,44,12,TFT_BLACK);text(4,22,"4 MAP",TFT_CYAN);
    // North-up indicator. No heading or road geometry is implied.
    canvas.fillRect(223,20,15,29,TFT_BLACK);text(227,22,"N");
    canvas.fillRect(230,35,1,11,TFT_LIGHTGREY);
    for(int i=0;i<4;++i){canvas.drawPixel(230-i,35+i,TFT_LIGHTGREY);canvas.drawPixel(230+i,35+i,TFT_LIGHTGREY);}
    // Black halo and cyan marker, exactly at the map viewport's center.
    canvas.fillRect(MidX-8,MidY-8,17,17,TFT_BLACK);
    for(int y=-5;y<=5;++y)for(int x=-5;x<=5;++x)
        if(std::abs(x)+std::abs(y)==5)canvas.drawPixel(MidX+x,MidY+y,TFT_CYAN);
    canvas.drawFastHLine(MidX-10,MidY,3,TFT_CYAN);canvas.drawFastHLine(MidX+8,MidY,3,TFT_CYAN);
    canvas.fillRect(MidX,MidY-10,1,3,TFT_CYAN);canvas.fillRect(MidX,MidY+8,1,3,TFT_CYAN);
    canvas.drawPixel(MidX,MidY,TFT_WHITE);
    // Current region name is native Shinonome 16x16; status/navigation stay native ASCII 8x8.
    const int label_width=PixelFont::width(name.c_str(),TextRole::Place);
    if(label_width>152)throw std::runtime_error("example place label too wide");
    canvas.fillRect(2,97,label_width+4,20,TFT_BLACK);
    font.draw(canvas,4,99,name.c_str(),TextRole::Place,TFT_WHITE);
    const int scale=std::lround(500/meters_x),right=231,left=right-scale;
    canvas.fillRect(left-3,97,scale+7,20,TFT_BLACK);text(right-32,98,"500m");
    canvas.drawFastHLine(left,111,scale+1,TFT_LIGHTGREY);
    canvas.fillRect(left,108,1,7,TFT_LIGHTGREY);canvas.fillRect(right,108,1,7,TFT_LIGHTGREY);
    canvas.drawFastHLine(0,120,240,TFT_WHITE);
    text(4,124,"1:LIVE 2:GPS 3:LOG ESC:SET");
    text(4,124,"1",TFT_CYAN);text(60,124,"2",TFT_CYAN);text(108,124,"3",TFT_CYAN);text(156,124,"ESC",TFT_CYAN);
    canvas.pushSprite(0,0);
    for(int y=0;y<=16;++y)for(int x=0;x<W;++x)display.pixels[y*W+x]=original[y*W+x];
    std::printf("P6\n240 135\n255\n");
    for(auto p:display.pixels){unsigned char b[]={static_cast<unsigned char>(((p>>11)&31)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};std::fwrite(b,1,3,stdout);}
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    dataset=ROOT.parent/'dataset/maps/e-stat_go_jp.A002005212020/build'
    parser.add_argument('--map',type=Path,default=dataset/'estat-r2ka-jgd2011-2020-grid-8192-10000.remap.wgsmapset.glc')
    parser.add_argument('--book',type=Path,default=dataset/'giswordbook/estat-r2ka-jgd2011-2020.giswordbook')
    args=parser.parse_args()
    renderer=(ROOT/'application/task/ui/UiRenderer.cpp').read_text()+(ROOT/'application/task/ui/UiCanvas.h').read_text()
    enums=(M5GFX/'v1/misc/enum.hpp').read_text()
    colors=[]
    for name in sorted(set(re.findall(r'\bTFT_[A-Z]+\b',SCENE+renderer))):
        value=re.search(rf'\b{name}\s*=\s*(0x[0-9A-Fa-f]+)',enums)[1]
        colors.append(f'inline constexpr uint16_t {name}={value};')
    with tempfile.TemporaryDirectory(prefix='reversegeoputer-map-proposal-') as directory:
        work=Path(directory);(work/'M5GFX.h').write_text(ADAPTER+'\n'+'\n'.join(colors));(work/'scene.cpp').write_text(SCENE)
        exe=work/'render'
        sources=[ROOT/'application/task/ui'/f'{name}.cpp' for name in ('PixelFont','UiRenderer','UsbRenderer','UiModel','LicenseText','SettingsText','Shinonome')]
        subprocess.run(['g++','-O2','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I',str(work),'-I',str(ROOT/'application'),'-I',str(ROOT/'tests/support'),'-I',str(ROOT/'third_party/galuchat/include'),'-I',str(M5GFX/'Fonts'),str(work/'scene.cpp'),*map(str,sources),str(ROOT/'application/Settings.cpp'),'-o',str(exe)],check=True,timeout=180)
        result=subprocess.run([str(exe),str(args.map),str(args.book)],stdout=subprocess.PIPE,check=True,timeout=90)
        ppm=work/'map.ppm';ppm.write_bytes(result.stdout)
        with Image.open(ppm) as source:image=source.convert('RGB')
        assert image.size==(240,135) and len(image.getcolors(16))<=9
        assert image.getpixel((120,68))==(255,255,255)
        out=ROOT/'doc/画面・操作/place-view';out.mkdir(exist_ok=True)
        image.save(out/'place-view-boundary-proposal.png')
        image.resize((960,540),Image.Resampling.NEAREST).save(out/'place-view-boundary-proposal-preview.png')
        print('Created native 240x135 and 4x nearest-neighbor preview')

if __name__=='__main__':main()

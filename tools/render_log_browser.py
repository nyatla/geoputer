"""Render production log-browser screens at Cardputer's native 240x135 resolution."""
from pathlib import Path
import re
import subprocess
import tempfile
from PIL import Image
from ui_preview_support import ROOT, M5GFX, ADAPTER, reference_frame

SCENE = r'''
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "task/ui/UiRenderer.h"
#include "dialog_preview_checks.h"
#include "task/ui/Shinonome.h"
using namespace geoputer;
int main(int argc,char** argv) {
    if(argc!=3)return 1;
    const bool english=!std::strcmp(argv[2],"en");
    const char* mode=argv[1];
    M5GFX display; UiRenderer renderer(display); if(!renderer.begin(previewColorDepth()))return 1;
    UiData data{}; data.settings=defaultSettings();data.settings.language=english?UiLanguage::English:UiLanguage::Japanese;data.storage.card_state=CardState::Mounted;
    data.browse.revision=1;data.browse.count=6;data.browse.has_older=true;data.browse.active_mask=1;
    for(unsigned n=0;n<6;++n)std::snprintf(data.browse.names[n],LogNameBytes,"202610%02u_093012_0.gps.dat",16-n);
    std::strcpy(data.browse.name,"20261016_093012_0.gps.dat");
    data.browse.summary={100,113376,1789309921000ull,1789321921000ull,3527,0};
    data.storage.log.session_id=100;data.storage.log.open=true;data.recording.state=RecordingState::Recording;
    auto screen=ScreenId::Logs;
    const bool usb_mode=!std::strncmp(mode,"usb",3);
    if(usb_mode){screen=ScreenId::UsbStorage;data.storage.usb.ready=true;}
    if(!std::strcmp(mode,"usb-error")){data.storage.usb.ready=false;data.storage.usb.io_error=true;}
    if(!std::strcmp(mode,"usb-ejected"))data.storage.usb.ejected=true;
    if(!std::strcmp(mode,"logger")||!std::strcmp(mode,"logger-paused")||!std::strcmp(mode,"logger-confirm"))screen=ScreenId::GpsLogger;
    if(!std::strcmp(mode,"logger-paused"))data.recording.state=RecordingState::Paused;
    if(!std::strcmp(mode,"pause-confirm"))screen=ScreenId::LivePlace;
    if(!std::strcmp(mode,"detail")||!std::strcmp(mode,"long-name")||!std::strcmp(mode,"partial-tail"))screen=ScreenId::LogDetail;
    if(!std::strcmp(mode,"point")||!std::strcmp(mode,"unknown")||!std::strcmp(mode,"loading")||!std::strcmp(mode,"crc-error")||!std::strcmp(mode,"bounds"))screen=ScreenId::LogPoint;
    if(!std::strcmp(mode,"empty")){data.browse.count=0;data.browse.has_older=false;}
    if(!std::strcmp(mode,"sd-error")){data.browse.count=0;data.browse.error=LogReadError::NoCard;}
    const bool delete_confirm=!std::strcmp(mode,"delete-confirm")||!std::strcmp(mode,"delete-selected")||!std::strcmp(mode,"delete-protected")||!std::strcmp(mode,"delete-short")||!std::strcmp(mode,"delete-longest");
    if(delete_confirm) {
        std::strcpy(data.browse.names[0],"undated_0123456789abcdef_4294967295.gps.dat");
        data.browse.active_mask=!std::strcmp(mode,"delete-protected")?1:0;
    }
    if(!std::strcmp(mode,"delete-short"))std::strcpy(data.browse.names[0],"20261017_093012_0.gps.dat");
    if(!std::strcmp(mode,"delete-longest"))std::strcpy(data.browse.names[0],"123456789012345678901234567890123456789.gps.dat");
    if(!std::strcmp(mode,"deleting")){data.browse.loading=data.browse.deleting=true;}
    if(!std::strcmp(mode,"delete-error")){data.browse.error=LogReadError::DeleteFailed;data.browse.active_mask=0;}
    if(!std::strcmp(mode,"long-name"))std::strcpy(data.browse.name,"undated_0123456789abcdef_4294967295.gps.dat");
    if(!std::strcmp(mode,"partial-tail"))data.browse.summary.tail=7;
    data.browse.index=data.browse.target=123;
    data.browse.point={35.681641,140.027194,0,true};data.browse.point.fields=126;
    data.browse.point.utc_epoch_ms=1789309921000ull;
    data.browse.point.speed_kmh=4.2;data.browse.point.course_deg=140;
    data.browse.point.altitude_m=12.3;data.browse.point.satellites=12;data.browse.point.hdop=0.8;
    if(!std::strcmp(mode,"unknown")){data.browse.point.fields=0;data.browse.point.utc_epoch_ms=UINT64_MAX;}
    if(!std::strcmp(mode,"loading"))data.browse.loading=true;
    if(!std::strcmp(mode,"crc-error")){data.browse.error=LogReadError::Corrupt;data.browse.point={};}
    if(!std::strcmp(mode,"bounds")){
        data.browse.point.latitude=-90;data.browse.point.longitude=180;
        data.browse.point.altitude_m=-2147483.648f;data.browse.point.hdop=655.35;
        data.browse.point.speed_kmh=2359.26;data.browse.point.course_deg=359.99;data.browse.point.satellites=255;
        data.browse.index=134217710;data.browse.summary.records=134217711;
    }
    UiModel model;model.setScreen({screen,DisplayMode::View,1,data,0,ActionResult::None});
    if(!std::strcmp(mode,"logger-confirm"))model.keyPressed(UiKey::Enter,1);
    if(!std::strcmp(mode,"pause-confirm"))model.keyPressed(UiKey::F1,1);
    if(usb_mode && (!std::strcmp(mode,"usb") || !std::strcmp(mode,"usb-selected")))model.keyPressed(UiKey::Escape,1);
    if(!std::strcmp(mode,"usb-selected"))model.keyPressed(UiKey::Right,2);
    if(delete_confirm)model.keyPressed(UiKey::DeleteLog,1);
    if(!std::strcmp(mode,"delete-selected"))model.keyPressed(UiKey::Right,2);
    renderAndCheckDialog(renderer,model,display);
    auto glyph=[&](int x,int y,uint32_t cp,uint16_t color) {
        for(int row=0;row<16;++row)for(int col=0;col<(english?8:16);++col) {
            const bool ink=english?(FontLib8x16[cp*16+row]&(0x80u>>col)):(shinonomeGlyph(cp)[row]&(0x8000u>>col));
            if(display.pixels.at((y+row)*240+x+col)!=(ink?color:TFT_BLACK))throw std::runtime_error("menu item is not using native language font");
        }
    };
    if(usb_mode && model.confirming()) {
        glyph(56,84,english?'C':U'取',!std::strcmp(mode,"usb-selected")?TFT_WHITE:TFT_CYAN);
        glyph(152,84,english?'E':U'終',!std::strcmp(mode,"usb-selected")?TFT_CYAN:TFT_WHITE);
    }
    if(screen==ScreenId::LogDetail)glyph(20,101,english?'R':U'再',TFT_WHITE);
    if(!std::strcmp(mode,"logger"))glyph(20,83,english?'S':U'停',TFT_CYAN);
    if(!std::strcmp(mode,"logger-paused"))glyph(20,83,english?'R':U'再',TFT_CYAN);
    if(!std::strcmp(mode,"logger-confirm")||!std::strcmp(mode,"pause-confirm"))glyph(56,80,english?'C':U'取',TFT_CYAN);
    if(delete_confirm) {
        const bool short_name=!std::strcmp(mode,"delete-short");
        const int y=short_name?80:85;
        glyph(56,y,english?'C':U'取',!std::strcmp(mode,"delete-selected")?TFT_WHITE:TFT_CYAN);
        glyph(152,y,english?'D':U'削',!std::strcmp(mode,"delete-protected")?TFT_DARKGREY:!std::strcmp(mode,"delete-selected")?TFT_CYAN:TFT_WHITE);
        for(int x=8;x<232;++x)for(int border:{short_name?38:33,short_name?102:107})
            if(display.pixels[border*240+x]!=TFT_WHITE)throw std::runtime_error("modal horizontal rule overwritten");
    }
    if(screen==ScreenId::Logs && !delete_confirm && data.browse.count) {
        const char* guide="[ENTER] OPEN    [D] DELETE";
        for(unsigned n=0;guide[n];++n) {
            const auto color=n<7?(data.browse.loading?TFT_DARKGREY:TFT_CYAN):
                n>=16&&n<19?(data.browse.loading||(data.browse.active_mask&1)?TFT_DARKGREY:TFT_CYAN):TFT_LIGHTGREY;
            for(int row=0;row<8;++row)for(int col=0;col<8;++col) {
                const bool ink=font8x8_c64[(guide[n]-32)*8+col]&(1u<<row);
                if(display.pixels[(124+row)*240+4+n*8+col]!=(ink?color:TFT_BLACK))throw std::runtime_error("list footer key guide differs from the specified pixel layout");
            }
        }
    }
    if(screen!=ScreenId::LivePlace)for(int y=0;y<135;++y)for(int x=236;x<240;++x)
        if(y!=22 && y!=118 && display.pixels[y*240+x]!=TFT_BLACK)throw std::runtime_error("text exceeded right margin");
    if(screen!=ScreenId::LivePlace)for(int x=0;x<240;++x)for(int y:{22,118})
        if(display.pixels[y*240+x]!=TFT_DARKGREY)throw std::runtime_error("header/footer border overwritten");
    std::printf("P6\n240 135\n255\n");
    for(auto c:display.pixels){const unsigned char pixel[]={static_cast<unsigned char>(((c>>11)&31)*255/31),static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};std::fwrite(pixel,1,3,stdout);}
}
'''

def main():
    renderer=(ROOT/'application/task/ui/UiRenderer.cpp').read_text()+(ROOT/'application/task/ui/UiCanvas.h').read_text()
    enums=(M5GFX/'v1/misc/enum.hpp').read_text()
    colors=[]
    for name in sorted(set(re.findall(r'\bTFT_[A-Z]+\b',SCENE+renderer))):
        value=re.search(rf'\b{name}\s*=\s*(0x[0-9A-Fa-f]+)',enums)[1]
        colors.append(f'inline constexpr uint16_t {name}={value};')
    modes=('usb','usb-screen','usb-selected','usb-error','usb-ejected','logger','logger-paused','logger-confirm','pause-confirm','list','delete-confirm','delete-selected','delete-protected','delete-short','delete-longest','deleting','delete-error','detail','point','long-name','unknown','loading','crc-error','empty','sd-error','partial-tail','bounds')
    with tempfile.TemporaryDirectory(prefix='geoputer-browser-') as directory:
        work=Path(directory);(work/'M5GFX.h').write_text(ADAPTER+'\n'+'\n'.join(colors));(work/'scene.cpp').write_text(SCENE)
        sources=[ROOT/'application/task/ui'/f'{name}.cpp' for name in ('PixelFont','UiRenderer','UsbRenderer','UiModel','LicenseText','SettingsText','Shinonome')]
        exe=work/'render'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(work),'-I',str(ROOT/'tools'),'-I',str(ROOT/'application'),'-I',str(ROOT/'tests/support'),'-I',str(M5GFX/'Fonts'),str(work/'scene.cpp'),*map(str,sources),str(ROOT/'application/Settings.cpp'),'-o',str(exe)],check=True,timeout=180)
        for language in ('ja','en'):
            for mode in modes:
                ppm=work/'screen.ppm'
                with ppm.open('wb') as target:target.write(reference_frame(exe,mode,language))
                with Image.open(ppm) as source: image=source.convert('RGB')
                folder=ROOT/'doc/画面・操作'/('usb-storage' if mode.startswith('usb') else 'live-place' if mode=='pause-confirm' else 'gps-logger' if mode.startswith('logger') else 'logs' if mode in ('list','empty','sd-error','delete-confirm','delete-selected','delete-protected','delete-short','delete-longest','deleting','delete-error') else 'log-detail')
                folder.mkdir(parents=True,exist_ok=True)
                stem=f'log-browser-{mode}' if language=='ja' else f'log-browser-en-{mode}'
                image.save(folder/f'{stem}.png')
                image.resize((960,540),Image.Resampling.NEAREST).save(folder/f'{stem}-preview.png')
            print(f'PASS: {language}, {len(modes)} scenes, native language fonts, 240x135 bounds and header/footer borders')

if __name__=='__main__':main()

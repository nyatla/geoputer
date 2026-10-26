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
    M5GFX display; UiRenderer renderer(display); alignas(4) uint8_t backing[240*135*2+4]{};
    const auto depth=previewColorDepth();
    const auto capacity=sizeof(backing);
    if(!renderer.begin(depth,std::getenv("GEOPUTER_PREVIEW_EXTERNAL") ? backing : nullptr,capacity))return 1;
    UiData data{}; data.settings=defaultSettings();data.settings.language=english?UiLanguage::English:UiLanguage::Japanese;data.storage.card_state=CardState::Mounted;
    data.settings.dataset=1;data.dataset.state=PlaceState::Ready;
    data.editBrowse().revision=1;data.editBrowse().count=6;data.editBrowse().has_older=true;data.editBrowse().active_mask=1;
    for(unsigned n=0;n<6;++n)std::snprintf(data.editBrowse().names[n],LogNameBytes,"202610%02u_093012_0.gps.dat",16-n);
    std::strcpy(data.editBrowse().name,"20261016_093012_0.gps.dat");
    data.editBrowse().summary={100,113376,1789309921000ull,1789321921000ull,3527,0};
    data.storage.log.session_id=100;data.storage.log.open=true;data.recording.state=RecordingState::Recording;
    auto screen=ScreenId::Logs;
    const bool usb_mode=!std::strncmp(mode,"usb",3);
    if(usb_mode){screen=ScreenId::UsbStorage;data.storage.usb.ready=true;}
    if(!std::strcmp(mode,"usb-error")){data.storage.usb.ready=false;data.storage.usb.io_error=true;}
    if(!std::strcmp(mode,"usb-ejected"))data.storage.usb.ejected=true;
    if(!std::strcmp(mode,"detail")||!std::strcmp(mode,"long-name")||!std::strcmp(mode,"partial-tail"))screen=ScreenId::LogDetail;
    if(!std::strcmp(mode,"point")||!std::strcmp(mode,"unknown")||!std::strcmp(mode,"loading")||!std::strcmp(mode,"crc-error")||!std::strcmp(mode,"bounds"))screen=ScreenId::LogPoint;
    if(!std::strcmp(mode,"empty")){data.editBrowse().count=0;data.editBrowse().has_older=false;}
    if(!std::strcmp(mode,"sd-error")){data.editBrowse().count=0;data.editBrowse().error=LogReadError::NoCard;}
    const bool delete_confirm=!std::strcmp(mode,"delete-confirm")||!std::strcmp(mode,"delete-selected")||!std::strcmp(mode,"delete-protected")||!std::strcmp(mode,"delete-short")||!std::strcmp(mode,"delete-longest");
    if(delete_confirm) {
        std::strcpy(data.editBrowse().names[0],"undated_0123456789abcdef_4294967295.gps.dat");
        data.editBrowse().active_mask=!std::strcmp(mode,"delete-protected")?1:0;
    }
    if(!std::strcmp(mode,"delete-short"))std::strcpy(data.editBrowse().names[0],"20261017_093012_0.gps.dat");
    if(!std::strcmp(mode,"delete-longest"))std::strcpy(data.editBrowse().names[0],"123456789012345678901234567890123456789.gps.dat");
    if(!std::strcmp(mode,"deleting")){data.editBrowse().loading=data.editBrowse().deleting=true;}
    if(!std::strcmp(mode,"delete-error")){data.editBrowse().error=LogReadError::DeleteFailed;data.editBrowse().active_mask=0;}
    if(!std::strcmp(mode,"long-name"))std::strcpy(data.editBrowse().name,"undated_0123456789abcdef_4294967295.gps.dat");
    if(!std::strcmp(mode,"partial-tail"))data.editBrowse().summary.tail=7;
    data.editBrowse().point_loaded=true;
    std::strcpy(data.place[0],"千葉県 習志野市");std::strcpy(data.place[1],"鷺沼2丁目1番1号");data.place_state=PlaceState::Ready;
    data.editBrowse().index=data.editBrowse().target=123;
    data.editBrowse().point={35.681641,140.027194,0,true};data.editBrowse().point.fields=126;
    data.editBrowse().point.utc_epoch_ms=1789309921000ull;
    data.editBrowse().point.speed_kmh=4.2;data.editBrowse().point.course_deg=140;
    data.editBrowse().point.altitude_m=12.3;data.editBrowse().point.satellites=12;data.editBrowse().point.hdop=0.8;
    if(!std::strcmp(mode,"unknown")){data.editBrowse().point.fields=0;data.editBrowse().point.utc_epoch_ms=UINT64_MAX;}
    if(!std::strcmp(mode,"loading"))data.editBrowse().loading=true;
    if(!std::strcmp(mode,"crc-error")){data.editBrowse().error=LogReadError::Corrupt;data.editBrowse().point={};}
    if(!std::strcmp(mode,"bounds")){
        data.editBrowse().point.latitude=-90;data.editBrowse().point.longitude=180;
        data.editBrowse().point.altitude_m=-2147483.648f;data.editBrowse().point.hdop=655.35;
        data.editBrowse().point.speed_kmh=2359.26;data.editBrowse().point.course_deg=359.99;data.editBrowse().point.satellites=255;
        data.editBrowse().index=134217710;data.editBrowse().summary.records=134217711;
    }
    const bool log_map=!std::strncmp(mode,"log-map",7);
    if(log_map) {screen=ScreenId::LogPoint;data.editBrowse().map_mode=true;}
    if(!std::strcmp(mode,"log-map-play"))data.editBrowse().playing=true;
    if(!std::strcmp(mode,"detail-no-dataset")) {screen=ScreenId::LogDetail;data.settings.dataset=0;data.dataset.state=PlaceState::Unselected;}
    if(!std::strcmp(mode,"log-map-loading"))data.editBrowse().loading=true;
    if(!std::strcmp(mode,"log-map-error"))data.map.error=map::Error::ReadError;
    UiModel model;model.setScreen({screen,DisplayMode::View,1,data,0,ActionResult::None});
    if(log_map) {
        map::FrameInfo info{};info.width=240;info.height=101;info.meters_per_pixel=11.132;
        auto& request=info.request;request.token=1;request.screen_generation=1;request.dataset_generation=data.dataset.generation;
        request.sample_seq=124;request.latitude=data.browse().point.latitude;request.longitude=data.browse().point.longitude;
        request.session=map::SessionId::Log;
        if(model.beginMapFrame(request,request.buffer)!=OperationStatus::Ok)throw std::runtime_error("log map begin");
        map::BoundaryImageView image(request.buffer.data,240,101);
        for(unsigned y=0;y<101;++y)for(unsigned x=0;x<240;++x)
            if(x==53 || x==176 || y==63 || x+y==140)image.set(x,y,2);
        if(model.commitMapFrame(info)!=OperationStatus::Ok)throw std::runtime_error("log map commit");
    }
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
    if(screen==ScreenId::LogDetail)glyph(20,97,english?'M':U'地',!std::strcmp(mode,"detail-no-dataset")?TFT_DARKGREY:TFT_WHITE);
    if(delete_confirm) {
        const bool short_name=!std::strcmp(mode,"delete-short");
        const int y=short_name?80:85;
        glyph(56,y,english?'C':U'取',!std::strcmp(mode,"delete-selected")?TFT_WHITE:TFT_CYAN);
        glyph(152,y,english?'D':U'削',!std::strcmp(mode,"delete-protected")?TFT_DARKGREY:!std::strcmp(mode,"delete-selected")?TFT_CYAN:TFT_WHITE);
        for(int x=8;x<232;++x)for(int border:{short_name?38:33,short_name?102:107})
            if(display.pixels[border*240+x]!=TFT_WHITE)throw std::runtime_error("modal horizontal rule overwritten");
    }
    if(screen==ScreenId::Logs && !delete_confirm) {
        // The former action-description row is empty. D is now a footer key.
        for(int y=97;y<105;++y)for(int x=0;x<240;++x)
            if(display.pixels[y*240+x]!=TFT_BLACK)throw std::runtime_error("obsolete log action prose remains");
        const auto color=data.editBrowse().deleting || data.editBrowse().loading || !data.editBrowse().count || (data.editBrowse().active_mask&1)?TFT_DARKGREY:TFT_WHITE;
        for(int row=0;row<8;++row)for(int col=0;col<8;++col) {
            const bool ink=font8x8_c64[('D'-32)*8+col]&(1u<<row);
            if(display.pixels[(124+row)*240+180+col]!=(ink?color:TFT_BLACK))throw std::runtime_error("D footer state differs from deletion availability");
        }
    }
    const int header=usb_mode?22:16;
    if(screen!=ScreenId::LivePlace && !log_map)for(int y=0;y<135;++y)for(int x=236;x<240;++x)
        if(y!=header && y!=118 && display.pixels[y*240+x]!=TFT_BLACK)throw std::runtime_error("text exceeded right margin");
    if(screen!=ScreenId::LivePlace)for(int x=0;x<240;++x)for(int y:{header,log_map?120:118})
        if(display.pixels[y*240+x]!=(log_map?TFT_WHITE:TFT_DARKGREY))throw std::runtime_error("header/footer border overwritten");
    std::printf("P6\n240 135\n255\n");
    for(auto c:display.pixels){const unsigned char pixel[]={static_cast<unsigned char>(((c>>11)&31)*255/31),static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};std::fwrite(pixel,1,3,stdout);}
}
'''

def main():
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'doc/画面・操作')
    args=parser.parse_args()
    renderer=(ROOT/'application/task/ui/UiRenderer.cpp').read_text()+(ROOT/'application/task/ui/UiCanvas.h').read_text()
    enums=(M5GFX/'v1/misc/enum.hpp').read_text()
    colors=[]
    for name in sorted(set(re.findall(r'\bTFT_[A-Z]+\b',SCENE+renderer))):
        value=re.search(rf'\b{name}\s*=\s*(0x[0-9A-Fa-f]+)',enums)[1]
        colors.append(f'inline constexpr uint16_t {name}={value};')
    modes=('usb','usb-screen','usb-selected','usb-error','usb-ejected','list','delete-confirm','delete-selected','delete-protected','delete-short','delete-longest','deleting','delete-error','detail','point','long-name','unknown','loading','crc-error','empty','sd-error','partial-tail','bounds','detail-no-dataset','log-map','log-map-play','log-map-loading','log-map-error')
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
                folder=args.output/('usb-storage' if mode.startswith('usb') else 'logs' if mode in ('list','empty','sd-error','delete-confirm','delete-selected','delete-protected','delete-short','delete-longest','deleting','delete-error') else 'log-detail')
                folder.mkdir(parents=True,exist_ok=True)
                stem=f'log-browser-{mode}' if language=='ja' else f'log-browser-en-{mode}'
                image.save(folder/f'{stem}.png')
                image.resize((960,540),Image.Resampling.NEAREST).save(folder/f'{stem}-preview.png')
            print(f'PASS: {language}, {len(modes)} scenes, native language fonts, 240x135 bounds and header/footer borders')

if __name__=='__main__':main()

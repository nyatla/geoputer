"""Render dataset list/detail/apply/error and non-blocking startup with production UI."""
from pathlib import Path
import io
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
#include "task/ui/EscapeIcon.h"
#include "task/ui/DatasetPresentation.h"
using namespace geoputer;
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    const bool ja=!std::strcmp(argv[2],"ja");const char* mode=argv[1];
    M5GFX display;UiRenderer renderer(display);if(!renderer.begin(previewColorDepth())) return 3;
    UiModel ui;UiData data{};data.settings=defaultSettings();data.settings.language=ja?UiLanguage::Japanese:UiLanguage::English;
    data.storage.settings_state=SettingsState::Loaded;data.storage.card_state=CardState::Mounted;
    ui.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    auto key=[&](UiKey k){ui.keyPressed(k,100);};
    auto publish=[&](){const auto action=ui.snapshot().action;++data.revision;data.editDatasets().revision=ui.data().datasets().revision+1;ui.updateContent({DisplayMode::View,1,data,action.action_id,ActionResult::Applied});};
    if(!std::strcmp(mode,"missing-boot")) {
        data.boot.complete=true;data.boot.dataset(PlaceState::DatasetMissing);data.settings.dataset=2;std::strcpy(data.settings.wordbook_language,"ja");
        ui.setScreen({ScreenId::Boot,DisplayMode::View,2,data,0,ActionResult::None});
    } else if(!std::strcmp(mode,"boot") || !std::strcmp(mode,"boot-ready")) {
        data.boot.set(BootItem::Display,BootCheckState::Passed,"240X135");
        data.boot.set(BootItem::Tasks,BootCheckState::Passed,"3T/4Q");
        data.boot.settings(SettingsState::Loaded);data.boot.card(CardState::Mounted);
        data.boot.set(BootItem::GPS,BootCheckState::Checking,"DEFERRED");
        if(!std::strcmp(mode,"boot-ready")) {
            data.boot.set(BootItem::Tasks,BootCheckState::Passed,"4T/5Q");
            data.boot.set(BootItem::GPS,BootCheckState::Passed,"UART OK");
            data.boot.dataset(PlaceState::Ready);data.boot.finish();
        }
        ui.setScreen({ScreenId::Boot,DisplayMode::View,2,data,0,ActionResult::None});
    } else if(!std::strcmp(mode,"maintenance") || !std::strcmp(mode,"restart") || !std::strcmp(mode,"restart-selected")) {
        data.boot.maintenance=true;
        ui.setScreen({ScreenId::Settings,DisplayMode::View,2,data,0,ActionResult::None});
        if(std::strcmp(mode,"maintenance"))key(UiKey::Escape);
        if(!std::strcmp(mode,"restart-selected"))key(UiKey::Right);
    } else {
        key(UiKey::Enter);for(unsigned i=0;i<5;++i)key(UiKey::Down);key(UiKey::Enter);
        data.editDatasets().count=4;data.editDatasets().has_newer=true;
        const char* titles_ja[]={"日本・市区町村","日本・町丁目","フランス行政区域","オーストラリア"};
        const char* titles_en[]={"Japan Municipalities","Japan Towns","France Administrative Areas","Australia"};
        for(unsigned i=0;i<4;++i){data.editDatasets().rows[i].number=i+1;std::snprintf(data.editDatasets().rows[i].title,sizeof(data.editDatasets().rows[i].title),"%s",ja?titles_ja[i]:titles_en[i]);}
        publish();
        if(std::strcmp(mode,"list")) {
            key(UiKey::Enter);data.editDatasets()={};data.editDatasets().detail=true;data.editDatasets().number=1;
            auto& d=data.editDatasets().info;std::snprintf(d.title,sizeof(d.title),"%s",ja?titles_ja[0]:titles_en[0]);
            std::snprintf(d.description,sizeof(d.description),"%s",ja?"日本全国の都道府県・市区町村を表示するデータセットです。":"Municipality boundaries throughout Japan.");
            std::strcpy(d.language,"ja");d.language_count=2;d.chunks=172;d.unit_inv_x=d.unit_inv_y=4096;
            d.west=122.9;d.east=153.99;d.south=20.42;d.north=45.55;publish();
            const auto count=datasetDetailRows(data.editDatasets(),ja);
            if(!std::strcmp(mode,"extent"))for(unsigned i=0;i<count-3;++i)key(UiKey::Down);
            if(!std::strcmp(mode,"apply") || !std::strcmp(mode,"blocked"))for(unsigned i=0;i<count-1;++i)key(UiKey::Down);
            if(!std::strcmp(mode,"blocked")){data.editDatasets().error=DatasetError::NotStopped;publish();}
            if(!std::strcmp(mode,"saving")){data.editDatasets().loading=data.editDatasets().applying=true;publish();}
        }
    }
    renderer.draw(ui);
    auto check_ascii=[&](int x,int y,const char* text,uint16_t color) {
        for(const auto* p=text;*p;++p,x+=8)for(int row=0;row<8;++row)for(int col=0;col<8;++col) {
            const bool ink=font8x8_c64[(*p-32)*8+col]&(1u<<row);
            if(display.pixels.at((y+row)*240+x+col)!=(ink?color:TFT_BLACK))throw std::runtime_error(std::string(mode)+": maintenance guide pixel mismatch at "+std::to_string(x+col)+","+std::to_string(y+row));
        }
    };
    const auto escape_color=ui.shortcutGuide()[0].tone==ShortcutTone::Disabled?TFT_DARKGREY:TFT_WHITE;
    for(unsigned y=0;y<escape_icon::Height;++y)for(unsigned x=0;x<escape_icon::Width;++x)
        if(display.pixels[(122+y)*240+4+x]!=(escape_icon::Rows[y]&(0x800000u>>x)?escape_color:TFT_BLACK))
            throw std::runtime_error("ESC mark must use its native ROM pixels");
    if(!std::strcmp(mode,"maintenance")) {
        check_ascii(4,4,"MAINTENANCE",TFT_LIGHTGREY);
    }
    if(ui.snapshot().screen==ScreenId::Settings)for(int x=0;x<240;++x)
        if(display.pixels.at(16*240+x)!=TFT_DARKGREY)throw std::runtime_error("settings header height mismatch");
    std::printf("P6\n240 135\n255\n");
    for(auto color:display.pixels) {
        const unsigned char rgb[]={static_cast<unsigned char>(((color>>11)&31)*255/31),static_cast<unsigned char>(((color>>5)&63)*255/63),static_cast<unsigned char>((color&31)*255/31)};
        std::fwrite(rgb,1,3,stdout);
    }
}
'''

def main():
    colors_source=(M5GFX/'v1/misc/enum.hpp').read_text()
    renderer=(ROOT/'application/task/ui/UiRenderer.cpp').read_text()+(ROOT/'application/task/ui/UiCanvas.h').read_text()
    colors=[]
    for name in sorted(set(re.findall(r'\bTFT_[A-Z]+\b',SCENE+renderer))):
        value=re.search(rf'\b{name}\s*=\s*(0x[0-9A-Fa-f]+)',colors_source)[1]
        colors.append(f'inline constexpr uint16_t {name}={value};')
    with tempfile.TemporaryDirectory(prefix='geoputer-dataset-render-') as directory:
        work=Path(directory);(work/'M5GFX.h').write_text(ADAPTER+'\n'+'\n'.join(colors));(work/'scene.cpp').write_text(SCENE)
        sources=[ROOT/'application/task/ui'/f'{name}.cpp' for name in ('PixelFont','UiRenderer','UsbRenderer','UiModel','LicenseText','SettingsText','Shinonome')]
        exe=work/'render'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(work),'-I',str(ROOT/'application'),'-I',str(ROOT/'tests/support'),'-I',str(M5GFX/'Fonts'),str(work/'scene.cpp'),*map(str,sources),str(ROOT/'application/Settings.cpp'),'-o',str(exe)],check=True,timeout=180)
        for language in ('ja','en'):
            for scene in ('list','detail','extent','apply','blocked','saving','missing-boot','boot','boot-ready','maintenance','restart','restart-selected'):
                frame=reference_frame(exe,scene,language)
                output=ROOT/'doc/画面・操作'/('boot' if scene in ('missing-boot','boot','boot-ready') else 'settings')/f'dataset-{language}-{scene}-preview.png'
                with Image.open(io.BytesIO(frame)) as image:
                    assert image.size==(240,135)
                    image.save(output)
                print(output.relative_to(ROOT))
if __name__=='__main__':
    main()

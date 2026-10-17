"""Render dataset list/detail/apply/error and fatal startup with production UI."""
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
    auto publish=[&](){const auto action=ui.snapshot().action;++data.revision;data.datasets.revision=ui.data().datasets.revision+1;ui.updateContent({DisplayMode::View,1,data,action.action_id,ActionResult::Applied});};
    if(!std::strcmp(mode,"fatal")) {
        data.boot.complete=data.boot.fatal_dataset=true;data.settings.dataset=2;std::strcpy(data.settings.wordbook_language,"ja");
        ui.setScreen({ScreenId::Boot,DisplayMode::View,2,data,0,ActionResult::None});
    } else {
        key(UiKey::Enter);for(unsigned i=0;i<5;++i)key(UiKey::Down);key(UiKey::Enter);
        data.datasets.count=4;data.datasets.has_newer=true;
        const char* titles_ja[]={"日本・市区町村","日本・町丁目","フランス行政区域","オーストラリア"};
        const char* titles_en[]={"Japan Municipalities","Japan Towns","France Administrative Areas","Australia"};
        for(unsigned i=0;i<4;++i){data.datasets.rows[i].number=i+1;std::snprintf(data.datasets.rows[i].title,sizeof(data.datasets.rows[i].title),"%s",ja?titles_ja[i]:titles_en[i]);}
        publish();
        if(std::strcmp(mode,"list")) {
            key(UiKey::Enter);data.datasets={};data.datasets.detail=true;data.datasets.number=1;
            auto& d=data.datasets.info;std::snprintf(d.title,sizeof(d.title),"%s",ja?titles_ja[0]:titles_en[0]);
            std::snprintf(d.description,sizeof(d.description),"%s",ja?"日本全国の都道府県・市区町村を表示するデータセットです。":"Municipality boundaries throughout Japan.");
            std::strcpy(d.language,"ja");d.language_count=2;d.chunks=172;d.unit_inv_x=d.unit_inv_y=4096;
            d.west=122.9;d.east=153.99;d.south=20.42;d.north=45.55;publish();
            const auto count=datasetDetailRows(data.datasets,ja);
            if(!std::strcmp(mode,"extent"))for(unsigned i=0;i<count-3;++i)key(UiKey::Down);
            if(!std::strcmp(mode,"apply") || !std::strcmp(mode,"blocked"))for(unsigned i=0;i<count-1;++i)key(UiKey::Down);
            if(!std::strcmp(mode,"blocked")){data.datasets.error=DatasetError::NotStopped;publish();}
            if(!std::strcmp(mode,"saving")){data.datasets.loading=data.datasets.applying=true;publish();}
        }
    }
    renderer.draw(ui);
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
        sources=[ROOT/'application/task/ui'/f'{name}.cpp' for name in ('PixelFont','UiRenderer','UiModel','LicenseText','SettingsText','Shinonome')]
        exe=work/'render'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(work),'-I',str(ROOT/'application'),'-I',str(ROOT/'tests/support'),'-I',str(M5GFX/'Fonts'),str(work/'scene.cpp'),*map(str,sources),str(ROOT/'application/Settings.cpp'),'-o',str(exe)],check=True,timeout=180)
        for language in ('ja','en'):
            for scene in ('list','detail','extent','apply','blocked','saving','fatal'):
                frame=reference_frame(exe,scene,language)
                output=ROOT/'doc/画面・操作'/('boot' if scene=='fatal' else 'settings')/f'dataset-{language}-{scene}-preview.png'
                with Image.open(io.BytesIO(frame)) as image:
                    assert image.size==(240,135)
                    image.save(output)
                print(output.relative_to(ROOT))
if __name__=='__main__':
    main()

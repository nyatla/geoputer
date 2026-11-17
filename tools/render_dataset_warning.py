"""Render unified dataset errors in the production Live Place name region."""
from pathlib import Path
import io
import re
import subprocess
import tempfile
from PIL import Image
from ui_preview_support import ROOT, M5GFX, ADAPTER, reference_frame, ui_cpp_sources, ui_renderer_source

SCENE = r'''
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "task/ui/GpsUiRenderer.h"
#include "task/ui/PlacePresentation.h"
using namespace geoputer;
int main(int argc, char** argv) {
    if(argc!=4)return 1;
    const bool ja=!std::strcmp(argv[1],"ja");
    const bool lost=!std::strcmp(argv[3],"lost");
    const struct {const char* name;PlaceState state;} causes[]={
        {"missing",PlaceState::DatasetMissing},{"metadata",PlaceState::MetadataError},
        {"version",PlaceState::VersionError},{"read",PlaceState::DataError},
        {"encoding",PlaceState::EncodingError},{"limit",PlaceState::TooManyChunks},
        {"memory",PlaceState::OutOfMemory},{"sd",PlaceState::NoCard}};
    PlaceState state=PlaceState::Ready;
    for(const auto& cause:causes)if(!std::strcmp(argv[2],cause.name))state=cause.state;
    if(state==PlaceState::Ready)return 2;
    UiData data{};data.settings=defaultSettings();data.settings.language=ja?UiLanguage::Japanese:UiLanguage::English;
    data.storage.card_state=state==PlaceState::NoCard?CardState::NoCard:CardState::Mounted;
    data.dataset={state,2000,config::MaxDatasetChunks,7};data.place_state=PlaceState::NoFix;
    std::strcpy(data.place[0],"OLD PLACE MUST NOT REMAIN");
    data.place_fix={0,0,0,true};
    data.gps.reception=lost?GpsReception::NoFix:GpsReception::Fix;
    data.gps.fix={35.681641,140.027194,19921000,!lost};
    data.gps.fix.fields=GpsSpeed|GpsCourse|GpsAltitude|GpsSatellites;
    data.gps.fix.speed_kmh=4.2;data.gps.fix.course_deg=140;data.gps.fix.altitude_m=12.3;data.gps.fix.satellites=12;
    M5GFX display;GpsUiRenderer renderer(display);if(!renderer.begin(previewColorDepth()))return 3;
    GpsUiModel model;model.setScreen({ScreenId::LivePlace,DisplayMode::View,1,data,0,ActionResult::None});
    renderer.draw(model);
    const auto message=datasetIssueText(state,ja);
    if(placeIssue(data)!=state || PixelFont::width(message.first,TextRole::Place)>232 ||
       PixelFont::width(message.second,TextRole::Place)>232)throw std::runtime_error("error reason or native text width");
    for(int row=0;row<2;++row) {
        const auto text=row?message.second:message.first;
        const int end=4+PixelFont::width(text,TextRole::Place);
        for(int y=20+row*20;y<36+row*20;++y)for(int x=end;x<240;++x)
            if(display.pixels.at(y*240+x)!=TFT_BLACK)throw std::runtime_error("stale name or line overflow");
    }
    for(int x=0;x<240;++x)if(display.pixels.at(60*240+x)!=TFT_WHITE)
        throw std::runtime_error("dataset error replaced the normal layout");
    if(!lost && state!=PlaceState::NoCard && mainError(data))throw std::runtime_error("dataset-only error created red band");
    model.keyPressed(UiKey::OpenSettings,1);
    if(model.snapshot().action.target!=ScreenId::Settings)throw std::runtime_error("dataset error blocked settings");
    std::printf("P6\n240 135\n255\n");
    for(auto color:display.pixels){const unsigned char p[]={static_cast<unsigned char>(((color>>11)&31)*255/31),
        static_cast<unsigned char>(((color>>5)&63)*255/63),static_cast<unsigned char>((color&31)*255/31)};std::fwrite(p,1,3,stdout);}
}
'''

def main():
    enums=(M5GFX/'v1/misc/enum.hpp').read_text()
    renderer=ui_renderer_source()
    colors=[]
    for name in sorted(set(re.findall(r'\bTFT_[A-Z]+\b',SCENE+renderer))):
        value=re.search(rf'\b{name}\s*=\s*(0x[0-9A-Fa-f]+)',enums)[1]
        colors.append(f'inline constexpr uint16_t {name}={value};')
    output=ROOT/'doc/dev/画面・操作/live-place'
    with tempfile.TemporaryDirectory(prefix='geoputer-dataset-errors-') as directory:
        work=Path(directory);(work/'M5GFX.h').write_text(ADAPTER+'\n'+'\n'.join(colors));(work/'scene.cpp').write_text(SCENE)
        sources=ui_cpp_sources()
        exe=work/'render'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=undefined','-fno-sanitize-recover=all',
            '-I',str(work),'-I',str(ROOT/'application'),'-I',str(ROOT/'tests/support'),'-I',str(M5GFX/'Fonts'),str(work/'scene.cpp'),
            *map(str,sources),str(ROOT/'application/Settings.cpp'),'-o',str(exe)],check=True,timeout=180)
        for lang in ('ja','en'):
            for kind in ('missing','metadata','version','read','encoding','limit','memory','sd'):
                images=[]
                for gps in ('fix','lost'):
                    frame=reference_frame(exe,lang,kind,gps)
                    with Image.open(io.BytesIO(frame)) as source:image=source.convert('RGB')
                    images.append(image)
                assert images[0].crop((4,20,236,56)).tobytes()==images[1].crop((4,20,236,56)).tobytes(),(lang,kind)
                stem=f'dataset-{kind}-{lang}'
                images[0].save(output/f'{stem}.png')
                images[0].resize((960,540),getattr(Image,"Resampling",Image).NEAREST).save(output/f'{stem}-preview.png')
        print('PASS: 8 dataset errors x 2 languages x Fix/loss, native 240x135, stable two-line causes, normal controls, 4bpp == 16bpp')

if __name__=='__main__':main()

"""Render the production boundary map UI from a real dataset (host, 240x135)."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile
from PIL import Image
from ui_preview_support import ROOT, M5GFX, ADAPTER, reference_frame

SCENE = r'''
#include "task/ui/UiRenderer.h"
#include "task/storage/MapBuildJob.h"
#include "support/MapIndexFixture.h"
#include "task/storage/galuchat/PlaceLookup.h"
#include "task/ui/PlacePresentation.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
using namespace geoputer;
void check(bool ok){if(!ok)throw std::runtime_error("map preview contract failed");}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    const char* mode=argv[1];
    test_index::generate(argv[2],"map.glc","names.giswordbook");
    map::Request request{};request.token=1;request.screen_generation=1;request.dataset_generation=1;
    request.latitude=35.681641;request.longitude=140.027194;request.scale=std::atoi(argv[3]);
    M5GFX display;UiRenderer renderer(display);alignas(4) uint8_t backing[240*135*2+4]{};
    const auto depth=previewColorDepth();
    const auto capacity=sizeof(backing);
    check(renderer.begin(depth,std::getenv("GEOPUTER_PREVIEW_EXTERNAL") ? backing : nullptr,capacity));
    UiData data{};data.settings=defaultSettings();data.settings.dataset=1;data.dataset.generation=1;data.map.scale=request.scale;
    data.storage.card_state=CardState::Mounted;data.storage.rg.state=RgCacheState::Ready;
    data.recording.state=RecordingState::Paused;data.place_state=PlaceState::Ready;
    CodeCacheBuffer lookup_pixels{};SelectedDataset lookup_dataset;PlaceLookup lookup(lookup_pixels,lookup_dataset);lookup.configure(argv[2],"ja");
    check(lookup.prepare().state==PlaceState::Ready);data.dataset=lookup.datasetInfo();
    request.dataset_generation=data.dataset.generation;
    data.gps.reception=GpsReception::Fix;data.gps.fix={request.latitude,request.longitude,19921000,true};
    UiModel model;model.tick(1000);model.setBattery(72,false,true);
    check(model.setScreen({ScreenId::Map,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok);
    const bool reading=!std::strcmp(mode,"reading");
    map::FrameInfo origin{};
    map::CodeImageBuffer session_pixels{};map::MapSession session(session_pixels);MapBuildJob job;
    if(!reading) {
        check(model.beginMapFrame(request,request.buffer)==OperationStatus::Ok);
        check(job.start(request,session));job.loaded(readBoundaryMap(lookup_dataset,request,0,nullptr,std::move(session)));
        for(int n=0;n<20&&!job.complete();++n)job.step();
        check(job.complete() && job.info().error==map::Error::None);
        origin=job.info();check(job.release(request));
        check(model.commitMapFrame(origin)==OperationStatus::Ok);
    }
    if(reading)data.map.busy=true;
    if(!std::strcmp(mode,"updating")){data.map.busy=true;data.map.scale=request.scale==4?2:request.scale*2;}
    if(!std::strcmp(mode,"no-fix"))data.map.error=map::Error::NoFix;
    if(!std::strcmp(mode,"read-error"))data.map.error=map::Error::ReadError;
    int marker_dx=0,marker_dy=0;
    if(!std::strcmp(mode,"cursor-near")){marker_dx=20;marker_dy=20;}
    if(!std::strcmp(mode,"cursor-edge")){marker_dx=32;marker_dy=32;}
    if(!std::strcmp(mode,"cursor-lost")){marker_dx=12;marker_dy=20;}
    data.gps.fix.latitude-=marker_dy*origin.meters_per_pixel/111320.;
    data.gps.fix.longitude+=marker_dx*origin.meters_per_pixel/(111320.*std::cos(origin.request.latitude*3.14159265358979323846/180));
    const auto current_place=lookup.search({1,2,data.gps.fix});
    check(current_place.state==PlaceState::Ready || current_place.state==PlaceState::NotFound);
    completePlacePresentation(data,current_place);
    data.revision=1;check(model.updateContent({DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok);
    if(!std::strcmp(mode,"cursor-lost")) {
        data.gps.fix.valid=false;data.map.error=map::Error::NoFix;++data.revision;
        check(model.updateContent({DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok);
    }
    renderer.draw(model);
    if(!reading) {
        check(display.pixels[(68+marker_dy)*240+120+marker_dx]==TFT_WHITE);
        check(display.pixels[(68+marker_dy)*240+125+marker_dx]==(data.map.error==map::Error::None?TFT_CYAN:TFT_DARKGREY));
    }
    for(int y=17;y<120;++y)for(int x=0;x<240;++x)check(display.pixels[y*240+x]!=TFT_YELLOW);
    auto zoom_key=[&](int left,char key,bool enabled) {
        for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
            const bool ink=font8x8_c64[(key-32)*8+x]&(1u<<y);
            check(display.pixels[(124+y)*240+left+x]==(ink?(enabled?TFT_WHITE:TFT_DARKGREY):TFT_BLACK));
        }
    };
    zoom_key(188,'+',data.map.scale>1);zoom_key(204,'-',data.map.scale<4);
    const char* ratio=map::scaleText(request.scale);
    for(unsigned n=0;ratio[n];++n)for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
        const bool ink=font8x8_c64[(ratio[n]-32)*8+x]&(1u<<y);
        if(ink)check(display.pixels[(22+y)*240+4+n*8+x]==TFT_CYAN);
    }
    if(!reading) {
        M5GFX expected;UiCanvas reference(&expected);check(reference.begin(previewColorDepth()));
        reference.fillSprite(TFT_BLACK);PixelFont font(expected);check(font.begin());
        font.draw(reference,4,88,data.place[0],TextRole::UI,TFT_WHITE);
        font.draw(reference,4,100,data.place[1],TextRole::Place,TFT_WHITE);
        reference.pushSprite(0,0);
        for(int y=88;y<116;++y)for(int x=4;x<236;++x)
            if(expected.pixels[y*240+x]==TFT_WHITE &&
               (std::abs(x-(120+marker_dx))>10 || std::abs(y-(68+marker_dy))>10))
                check(display.pixels[y*240+x]==TFT_WHITE);
        // Outside the glyphs and their one-pixel outlines the map must survive,
        // including the old opaque caption strip and the former right-hand scale.
        for(int y=40;y<119;++y)for(int x=0;x<240;++x) {
            if(x>=223 && y<=47)continue; // north arrow
            if(data.map.error!=map::Error::None && y>=47 && y<=56)continue;
            if(std::abs(x-(120+marker_dx))<=11 && std::abs(y-(68+marker_dy))<=11)continue;
            bool label=false;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
                if(x+dx>=0 && x+dx<240 && y+dy>=0 && y+dy<135 && expected.pixels[(y+dy)*240+x+dx])label=true;
            if(!label) {
                const auto pixel=model.mapFrame().image().get(x,y-18);
                check(display.pixels[y*240+x]==(pixel==2?TFT_WHITE:pixel?TFT_DARKGREY:TFT_BLACK));
            }
        }
    }
    for(int x=0;x<240;++x)check(display.pixels[120*240+x]==TFT_WHITE);
    std::printf("P6\n240 135\n255\n");
    for(auto p:display.pixels) {const unsigned r=(p>>11)&31,g=(p>>5)&63,b=p&31;
        std::putchar((r<<3)|(r>>2));std::putchar((g<<2)|(g>>4));std::putchar((b<<3)|(b>>2));}
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('map',type=Path);parser.add_argument('wordbook',type=Path)
    parser.add_argument('--scales',type=int,nargs='+',choices=(1,2,4),default=[1])
    parser.add_argument('--output',type=Path,default=ROOT/'doc/画面・操作/place-view')
    args=parser.parse_args()
    enum_source=(M5GFX/'v1/misc/enum.hpp').read_text()
    renderer_source=(ROOT/'application/task/ui/UiRenderer.cpp').read_text()+(ROOT/'application/task/ui/UiCanvas.h').read_text()
    colors=[]
    for name in sorted(set(re.findall(r'\bTFT_[A-Z]+\b',SCENE+renderer_source))):
        value=re.search(rf'\b{name}\s*=\s*(0x[0-9A-Fa-f]+)',enum_source)
        colors.append(f'inline constexpr uint16_t {name} = {value[1]};')
    sources=['Settings.cpp','task/ui/PixelFont.cpp','task/ui/UiRenderer.cpp','task/ui/UsbRenderer.cpp',
        'task/ui/UiModel.cpp','task/ui/LicenseText.cpp','task/ui/SettingsText.cpp','task/ui/Shinonome.cpp',
        'task/storage/dataset/DatasetIndex.cpp','task/storage/dataset/DatasetMetadata.cpp',
        'task/storage/galuchat/MapSource.cpp','task/storage/galuchat/DiskIndexedMapReader.cpp',
        'task/storage/galuchat/DiskIndexedWordBookReader.cpp','task/storage/galuchat/TextEncoding.cpp',
        'task/storage/galuchat/PlaceLookup.cpp','task/storage/galuchat/CachedMapReader.cpp']
    with tempfile.TemporaryDirectory(prefix='reversegeoputer-map-preview-') as folder:
        work=Path(folder);data=work/'data';data.mkdir()
        shutil.copyfile(args.map,data/'map.glc');shutil.copyfile(args.wordbook,data/'names.giswordbook')
        (data/'dataset.ini').write_text('[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=Preview\nwordbook=names.giswordbook\ndisplay_line1=%0 %1\ndisplay_line2=%2%3%4%5\n')
        (work/'M5GFX.h').write_text(ADAPTER+'\n'+'\n'.join(colors));(work/'scene.cpp').write_text(SCENE)
        executable=work/'render'
        subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pedantic',
            '-I',str(work),'-I',str(ROOT/'tests'),'-I',str(ROOT/'tests/support'),'-I',str(ROOT/'application'),
            '-I',str(ROOT/'third_party/galuchat/include'),'-I',str(M5GFX/'Fonts'),str(work/'scene.cpp'),
            *[str(ROOT/'application'/s) for s in sources],'-o',str(executable)],check=True,timeout=180)
        output=args.output;output.mkdir(parents=True,exist_ok=True)
        for scale in args.scales:
            complete_frame=None
            for mode in ('ready','updating','reading','no-fix','read-error','cursor-near','cursor-edge','cursor-lost'):
                suffix='' if scale==1 else f'1-{scale}-'
                ppm=work/f'{scale}-{mode}.ppm';ppm.write_bytes(reference_frame(executable,mode,str(data),str(scale)))
                with Image.open(ppm) as source:
                    image=source.convert('RGB')
                    if mode=='ready':complete_frame=image.crop((0,0,240,121)).tobytes()
                    if mode=='updating' and image.crop((0,0,240,121)).tobytes()!=complete_frame:raise AssertionError('updating must preserve the displayed map; footer +/- keys follow the requested scale')
                    image.save(output/f'map-view-{suffix}{mode}.png')
                    image.resize((960,540),Image.Resampling.NEAREST).save(output/f'map-view-{suffix}{mode}-preview.png')
                print(f'PASS: scale=1/{scale} {mode}, indexed and RGB565 renderers match at native 240x135')
if __name__=='__main__':main()

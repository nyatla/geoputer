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
    MapBuildJob job;check(job.start(request));job.loaded(readBoundaryMap(argv[2],"ja",request,0,nullptr));
    for(int n=0;n<20&&!job.complete();++n)job.step();
    check(job.complete() && job.info().error==map::Error::None);
    M5GFX display;UiRenderer renderer(display);check(renderer.begin(previewColorDepth()));
    UiData data{};data.settings=defaultSettings();data.settings.dataset=1;data.dataset.generation=1;data.map.scale=request.scale;
    data.storage.card_state=CardState::Mounted;data.storage.rg.state=RgCacheState::Ready;
    data.recording.state=RecordingState::Paused;data.place_state=PlaceState::Ready;
    data.gps.reception=GpsReception::Fix;data.gps.fix={request.latitude,request.longitude,19921000,true};
    UiModel model;model.tick(1000);model.setBattery(72,false,true);
    check(model.setScreen({ScreenId::Map,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok);
    map::Transfer transfer{};transfer.info=job.info();
    check(model.beginMapFrame(transfer)==OperationStatus::Ok);
    for(unsigned row=0;row<101;row+=8) {
        request.row=row;check(job.read(request,transfer));check(model.writeMapRows(transfer)==OperationStatus::Ok);
    }
    check(model.commitMapFrame(transfer)==OperationStatus::Ok);
    if(!std::strcmp(mode,"updating")){data.map.busy=true;data.map.scale=request.scale==4?2:request.scale*2;}
    if(!std::strcmp(mode,"no-fix"))data.map.error=map::Error::NoFix;
    if(!std::strcmp(mode,"read-error"))data.map.error=map::Error::ReadError;
    data.revision=1;check(model.updateContent({DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok);
    renderer.draw(model);
    check(display.pixels[68*240+120]==TFT_WHITE);
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
        'task/storage/galuchat/DiskIndexedWordBookReader.cpp','task/storage/galuchat/TextEncoding.cpp']
    with tempfile.TemporaryDirectory(prefix='reversegeoputer-map-preview-') as folder:
        work=Path(folder);data=work/'data';data.mkdir()
        shutil.copyfile(args.map,data/'map.glc');shutil.copyfile(args.wordbook,data/'names.giswordbook')
        (data/'dataset.ini').write_text('[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=Preview\nwordbook=names.giswordbook\n')
        (work/'M5GFX.h').write_text(ADAPTER+'\n'+'\n'.join(colors));(work/'scene.cpp').write_text(SCENE)
        executable=work/'render'
        subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pedantic',
            '-I',str(work),'-I',str(ROOT/'tests'),'-I',str(ROOT/'tests/support'),'-I',str(ROOT/'application'),
            '-I',str(ROOT/'third_party/galuchat/include'),'-I',str(M5GFX/'Fonts'),str(work/'scene.cpp'),
            *[str(ROOT/'application'/s) for s in sources],'-o',str(executable)],check=True,timeout=180)
        output=args.output;output.mkdir(parents=True,exist_ok=True)
        for scale in args.scales:
            for mode in ('ready','updating','no-fix','read-error'):
                suffix='' if scale==1 else f'1-{scale}-'
                ppm=work/f'{scale}-{mode}.ppm';ppm.write_bytes(reference_frame(executable,mode,str(data),str(scale)))
                with Image.open(ppm) as source:
                    image=source.convert('RGB');image.save(output/f'map-view-{suffix}{mode}.png')
                    image.resize((960,540),Image.Resampling.NEAREST).save(output/f'map-view-{suffix}{mode}-preview.png')
                print(f'PASS: scale=1/{scale} {mode}, indexed and RGB565 renderers match at native 240x135')
if __name__=='__main__':main()

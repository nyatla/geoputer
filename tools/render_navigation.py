"""Render production MENU, GPS details and shared controls at 240x135."""
from pathlib import Path
import re,subprocess,tempfile
from PIL import Image
from ui_preview_support import ROOT,M5GFX,ADAPTER,reference_frame
SCENE=r"""
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "task/ui/GpsUiRenderer.h"
using namespace geoputer;
int main(int argc,char** argv){
    if(argc!=3)return 1;
    const bool ja=!std::strcmp(argv[2],"ja");const char* mode=argv[1];
    M5GFX display;GpsUiRenderer renderer(display);if(!renderer.begin(previewColorDepth()))return 1;
    UiData data{};data.settings=defaultSettings();data.settings.language=ja?UiLanguage::Japanese:UiLanguage::English;
    data.storage.card_state=CardState::Mounted;data.recording.state=RecordingState::Recording;
    data.recording.elapsed_ms=3*3600000+42*60000+18000;data.recording.now_ms=120000;
    data.storage.log.saved=13336;data.storage.log.buffered=2;
    std::strcpy(data.storage.log.name,"20261025_093012_0.gps.dat");
    auto& gps=data.gps;gps.reception=GpsReception::Fix;gps.received=true;gps.received_at_ms=119000;
    gps.fix={35.681641,140.027194,0,true};gps.fix.fields=GpsAltitude|GpsSpeed|GpsCourse|GpsHdop|GpsSatellites;
    gps.fix.altitude_m=12.3;gps.fix.speed_kmh=4.2;gps.fix.course_deg=140;gps.fix.hdop=0.8;gps.fix.satellites=12;
    if(!std::strcmp(mode,"paused"))data.recording.state=RecordingState::Paused;
    if(!std::strcmp(mode,"stopped"))data.recording.state=RecordingState::Stopped;
    if(!std::strcmp(mode,"boot-active"))data.recording.state=RecordingState::Stopped;
    if(!std::strcmp(mode,"no-sd")){data.recording.state=RecordingState::Stopped;data.storage.card_state=CardState::NoCard;}
    if(!std::strcmp(mode,"zero")){gps.fix.latitude=0;gps.fix.longitude=0;gps.fix.speed_kmh=0;gps.fix.course_deg=0;gps.fix.altitude_m=0;gps.fix.satellites=0;data.storage.log.saved=0;data.recording.dropped=0;}
    if(!std::strcmp(mode,"wide")){gps.fix.latitude=-90;gps.fix.longitude=-180;gps.fix.speed_kmh=999;gps.fix.course_deg=359;gps.fix.altitude_m=-12345.6;gps.fix.hdop=999.9;gps.fix.satellites=255;data.storage.log.saved=4294967295u;data.recording.dropped=4294967295u;}
    if(!std::strcmp(mode,"no-fix")){gps.reception=GpsReception::NoFix;gps.fix.valid=false;gps.fix.fields=GpsSatellites|GpsHdop;}
    if(!std::strcmp(mode,"long-name"))std::strcpy(data.storage.log.name,"undated_0123456789abcdef_4294967295.gps.dat");
    if(!std::strcmp(mode,"save-error"))data.storage.log.error=LogError::Io;
    const auto screen=!std::strcmp(mode,"menu")?ScreenId::Menu:ScreenId::GpsLogger;
    GpsUiModel model;model.tick(120000);model.setScreen({screen,DisplayMode::View,1,data,0,ActionResult::None});
    const bool dialog=std::strcmp(mode,"menu") && std::strcmp(mode,"details") && std::strcmp(mode,"zero") &&
        std::strcmp(mode,"wide") && std::strcmp(mode,"no-fix") && std::strcmp(mode,"long-name") && std::strcmp(mode,"save-error");
    if(dialog){model.keyPressed(UiKey::Enter,120001);if(!std::strcmp(mode,"stop-selected")){model.keyPressed(UiKey::Left,120002);}}
    if(dialog) {
        const auto state=data.recording.state;
        const auto count=state==RecordingState::Stopped?1u:2u;
        const auto selected=!std::strcmp(mode,"stop-selected") || state==RecordingState::Stopped?0u:1u;
        if(model.dialog().choices()!=count || model.dialog().choice()!=selected)throw std::runtime_error("GPS action menu count/default differs");
        if(!std::strcmp(mode,"no-sd") && model.dialog().choiceEnabled(0))throw std::runtime_error("Start must be disabled without SD");
    }
    renderer.draw(model);
    const int header=screen==ScreenId::GpsLogger?16:22;
    for(int x=0;x<240;++x)for(int y:{header,118})if(display.pixels[y*240+x]!=TFT_DARKGREY)throw std::runtime_error("header/footer overwritten");
    for(int y=0;y<135;++y)for(int x=236;x<240;++x)if(y!=header&&y!=118&&display.pixels[y*240+x]!=TFT_BLACK)throw std::runtime_error("right margin exceeded");
    if(screen==ScreenId::GpsLogger && !dialog)for(int y=111;y<118;++y)for(int x=0;x<240;++x)
        if(display.pixels[y*240+x]!=TFT_BLACK)throw std::runtime_error("obsolete GPS action prose remains");
    if(dialog)for(int x=8;x<232;++x)for(int y:{34,103})if(display.pixels[y*240+x]!=TFT_WHITE)throw std::runtime_error("dialog rule overwritten");
    std::printf("P6\n240 135\n255\n");
    for(auto c:display.pixels){const unsigned char rgb[]={static_cast<unsigned char>(((c>>11)&31)*255/31),static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};std::fwrite(rgb,1,3,stdout);}
}
"""
def main():
    renderer=(ROOT/'application/task/ui/GpsUiRenderer.cpp').read_text()+(ROOT/'application/task/ui/UiCanvas.h').read_text()
    enums=(M5GFX/'v1/misc/enum.hpp').read_text();colors=[]
    for name in sorted(set(re.findall(r'\bTFT_[A-Z]+\b',SCENE+renderer))):
        value=re.search(rf'\b{name}\s*=\s*(0x[0-9A-Fa-f]+)',enums)[1];colors.append(f'inline constexpr uint16_t {name}={value};')
    with tempfile.TemporaryDirectory(prefix='geoputer-navigation-') as directory:
        work=Path(directory);(work/'M5GFX.h').write_text(ADAPTER+'\n'+'\n'.join(colors));(work/'scene.cpp').write_text(SCENE)
        sources=[ROOT/'application/task/ui'/f'{name}.cpp' for name in ('PixelFont','GpsUiRenderer','GpsUiModel','LicenseText','SettingsText','Shinonome')]
        exe=work/'render'
        subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=undefined','-fno-sanitize-recover=all',
            '-I',str(work),'-I',str(ROOT/'application'),'-I',str(ROOT/'tests/support'),'-I',str(M5GFX/'Fonts'),str(work/'scene.cpp'),*map(str,sources),str(ROOT/'application/Settings.cpp'),'-o',str(exe)],check=True,timeout=180)
        for language in ('ja','en'):
            fixed_regions=None
            for mode in ('menu','details','recording','stop-selected','paused','stopped','boot-active','no-sd','zero','wide','no-fix','long-name','save-error'):
                frame=reference_frame(exe,mode,language);ppm=work/'frame.ppm';ppm.write_bytes(frame)
                with Image.open(ppm) as source:image=source.convert('RGB')
                if mode in ('details','zero','wide','no-fix'):
                    boxes=[(4,23,28,31),(124,23,148,31),(4,33,28,41),(124,33,148,41),(228,33,236,41),
                           (4,43,28,51),(108,43,116,51),(124,43,156,51),(4,53,28,61),(84,53,116,61),
                           (124,53,148,61),(212,53,236,61),(4,63,28,71),(4,73,28,81),(124,73,148,81),
                           (212,73,236,81),(4,83,44,91),(124,83,156,91)]
                    regions=[image.crop(box).tobytes() for box in boxes]
                    if fixed_regions is None:fixed_regions=regions
                    elif regions!=fixed_regions:raise AssertionError('GPS labels or units moved with value widths')
                if mode not in ('menu','details','paused','recording'):
                    continue
                folder=ROOT/'doc/dev/画面・操作'/('menu' if mode=='menu' else 'gps-logger');folder.mkdir(exist_ok=True)
                if mode=='menu':stem=f'{language}-menu'
                else:
                    suffix={'details':'logger','paused':'logger-paused','recording':'logger-confirm'}[mode]
                    stem='log-browser-'+('en-' if language=='en' else '')+suffix
                image.save(folder/f'{stem}.png');image.resize((960,540),getattr(Image,"Resampling",Image).NEAREST).save(folder/f'{stem}-preview.png')
            print(f'PASS: {language}, 13 scenes, bounds/rules and 4-bit/RGB565 equality')
if __name__=='__main__':main()

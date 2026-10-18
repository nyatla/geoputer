"""Render the production USB/export screens at native Cardputer resolution."""
from pathlib import Path
from io import BytesIO
import re
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont
from ui_preview_support import ROOT, M5GFX, ADAPTER, reference_frame

OUTPUT = ROOT / 'doc/画面・操作/usb-storage/export-proposal'
SCENES = ('connected', 'disconnected', 'eject', 'files', 'format', 'progress', 'complete-connected', 'complete')
LABELS = ('1  PC CONNECTED', '2  NO PC CONNECTION', '3  SAFE REMOVAL / CONNECTED ONLY',
          '4  SELECT LOG', '5  SELECT FORMAT', '6  EXPORTING',
          '7  COMPLETE / PC CONNECTED', '8  COMPLETE / NO PC CONNECTION')

SCENE = r'''
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "task/ui/UiRenderer.h"
using namespace geoputer;
int main(int argc,char** argv) {
    if(argc!=3)return 1;
    const char* mode=argv[1];const bool en=!std::strcmp(argv[2],"en");
    M5GFX display;UiRenderer renderer(display);if(!renderer.begin(previewColorDepth()))return 2;
    UiData data{};data.revision=1;data.settings=defaultSettings();
    data.settings.language=en?UiLanguage::English:UiLanguage::Japanese;
    data.storage.usb.ready=true;
    data.storage.usb.connected=std::strcmp(mode,"disconnected") && std::strcmp(mode,"complete");
    data.browse.revision=1;data.browse.count=4;data.browse.has_older=true;
    const char* names[]={"20261017_093012_0.gps.dat","20261016_142801_0.gps.dat","20261015_081530_0.gps.dat","20261014_175205_0.gps.dat"};
    for(unsigned n=0;n<4;++n)std::strcpy(data.browse.names[n],names[n]);
    std::strcpy(data.storage.export_job.name,names[0]);data.storage.export_job.format=ExportFormat::Gpx;
    if(!std::strcmp(mode,"no-sd"))data.storage.usb.ready=false;
    if(!std::strcmp(mode,"ejected"))data.storage.usb.ejected=true;
    UiModel ui;ui.setScreen({ScreenId::UsbStorage,DisplayMode::View,1,data,0,ActionResult::None});
    auto& job=data.storage.export_job;
    if(!std::strcmp(mode,"eject"))job.phase=ExportPhase::WaitEject;
    if(!std::strcmp(mode,"files")||!std::strcmp(mode,"format")||!std::strcmp(mode,"long-format")||!std::strcmp(mode,"empty")||!std::strcmp(mode,"loading")) {
        job.phase=ExportPhase::Selecting;data.storage.usb.local=true;
        if(!std::strcmp(mode,"empty"))data.browse.count=0;
        if(!std::strcmp(mode,"loading"))data.browse.loading=true;
        if(!std::strcmp(mode,"long-format"))std::strcpy(data.browse.names[0],"123456789012345678901234567890123456789.gps.dat");
    }
    if(!std::strcmp(mode,"progress")||!std::strcmp(mode,"full-progress")) {
        job.phase=ExportPhase::Running;data.storage.usb.local=true;job.total=4096;job.processed=2048;
        if(!std::strcmp(mode,"full-progress"))job.total=job.processed=134217711;
    }
    if(!std::strcmp(mode,"complete")||!std::strcmp(mode,"complete-connected")||!std::strcmp(mode,"long-complete")) {
        job.phase=ExportPhase::Complete;job.output_created=true;
        if(!std::strcmp(mode,"long-complete"))std::strcpy(job.name,"123456789012345678901234567890123456789.gps.dat");
    }
    if(!std::strcmp(mode,"error")) {job.phase=ExportPhase::Failed;job.error=ExportError::Corrupt;job.failed_record=12;}
    if(!std::strcmp(mode,"publish-error")) {job.phase=ExportPhase::Failed;job.output_created=true;job.error=ExportError::Publish;}
    ui.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    if(!std::strcmp(mode,"format")||!std::strcmp(mode,"long-format"))ui.keyPressed(UiKey::Enter,1);
    renderer.draw(ui);
    for(int y=0;y<135;++y)for(int x=236;x<240;++x)
        if(y!=22 && y!=118 && display.pixels[y*240+x]!=TFT_BLACK)throw std::runtime_error("right margin overwritten");
    for(int x=0;x<240;++x)for(int y:{22,118})
        if(display.pixels[y*240+x]!=TFT_DARKGREY)throw std::runtime_error("header/footer border overwritten");
    std::printf("P6\n240 135\n255\n");
    for(const auto color:display.pixels) {
        const unsigned char rgb[]={static_cast<unsigned char>(((color>>11)&31)*255/31),
                                   static_cast<unsigned char>(((color>>5)&63)*255/63),
                                   static_cast<unsigned char>((color&31)*255/31)};
        std::fwrite(rgb,1,3,stdout);
    }
}
'''


def overview(frames, language):
    # Captions and padding belong to the comparison sheet, outside device pixels.
    scale, pad, caption = 3, 24, 28
    width, height = 240 * scale, 135 * scale
    rows=(len(frames)+1)//2
    sheet = Image.new('RGB', (2*width+3*pad, rows*(height+caption)+(rows+1)*pad), '#161b22')
    draw = ImageDraw.Draw(sheet)
    face = ImageFont.load_default(size=16)
    for index, (frame, label) in enumerate(zip(frames, LABELS)):
        x = pad + (index % 2) * (width + pad)
        y = pad + (index // 2) * (height + caption + pad)
        draw.text((x, y), label, fill='#dce4ed', font=face)
        sheet.paste(frame.resize((width, height), Image.Resampling.NEAREST), (x, y+caption))
    sheet.save(OUTPUT/f'usb-export-{language}-overview.png')


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    enums = (M5GFX/'v1/misc/enum.hpp').read_text()
    colors = []
    for name in sorted(set(re.findall(r'\bTFT_[A-Z]+\b', SCENE+(ROOT/'application/task/ui/UiCanvas.h').read_text()+(ROOT/'application/task/ui/UsbRenderer.cpp').read_text()))):
        value = re.search(rf'\b{name}\s*=\s*(0x[0-9A-Fa-f]+)', enums)[1]
        colors.append(f'inline constexpr uint16_t {name}={value};')
    with tempfile.TemporaryDirectory(prefix='geoputer-usb-proposal-') as directory:
        work = Path(directory)
        (work/'M5GFX.h').write_text(ADAPTER+'\n'+'\n'.join(colors))
        (work/'scene.cpp').write_text(SCENE)
        exe = work/'render'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic',
                        '-fsanitize=undefined','-fno-sanitize-recover=all',
                        '-I',str(work),'-I',str(ROOT/'application'),'-I',str(M5GFX/'Fonts'),
                        str(work/'scene.cpp'),*[str(ROOT/'application/task/ui'/f'{name}.cpp') for name in ('PixelFont','UiRenderer','UsbRenderer','UiModel','LicenseText','SettingsText','Shinonome')],
                        str(ROOT/'application/Settings.cpp'),'-I',str(ROOT/'tests/support'),'-o',str(exe)],check=True,timeout=180)
        for language in ('ja','en'):
            frames=[]
            for mode in (*SCENES,'error','publish-error','no-sd','ejected','long-format','long-complete','empty','loading','full-progress'):
                with Image.open(BytesIO(reference_frame(exe,mode,language))) as source:
                    image=source.convert('RGB')
                assert image.size==(240,135)
                if mode in ('long-format','long-complete','empty','loading','full-progress'):continue
                stem=f'usb-export-{language}-{mode}'
                image.save(OUTPUT/f'{stem}.png')
                image.resize((960,540),Image.Resampling.NEAREST).save(OUTPUT/f'{stem}-preview.png')
                frames.append(image)
            overview(frames[:len(SCENES)],language)
            print(f'PASS: {language}, 17 production USB states, native fonts, long names/counts, bounds, 4bpp == 16bpp')


if __name__=='__main__':
    main()

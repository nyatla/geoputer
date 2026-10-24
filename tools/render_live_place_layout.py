"""Render the actual firmware UiRenderer with example data and native pixel fonts.

This is host execution of production drawing code, not a device screen capture.
The application sources are compiled unchanged.
Run: python3 tools/render_live_place_layout.py
"""

from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image

from ui_preview_support import ADAPTER, M5GFX, OUTPUT, ROOT, reference_frame


SCENE = r'''
#include <cstdio>
#include <cstring>
#include "task/ui/UiRenderer.h"
#include "task/ui/LivePlaceLayout.h"
#include "task/ui/PlacePresentation.h"
#include "task/ui/Shinonome.h"
#include "task/storage/galuchat/TextEncoding.h"
#include <stdexcept>
#include <limits>
using namespace geoputer;
int main(int argc, char** argv) {
    const char* mode = argc == 2 ? argv[1] : "rec";
    const bool rebuilding = std::strcmp(mode,"reload") == 0;
    const bool unprepared = std::strcmp(mode,"unprepared") == 0;
    const bool paused = std::strcmp(mode,"pause") == 0 || std::strcmp(mode,"sd-read") == 0;
    const bool sd_error = std::strcmp(mode,"sd-error") == 0;
    const bool gps_error = std::strcmp(mode,"gps-error") == 0;
    const bool io = rebuilding || std::strcmp(mode,"sd-write") == 0 || std::strcmp(mode,"sd-read") == 0 || std::strcmp(mode,"dictionary-read") == 0;
    const bool long_speed = std::strcmp(mode,"speed-long") == 0;
    const bool full_battery = std::strcmp(mode,"full-battery") == 0;
    const bool missing = std::strcmp(mode,"missing") == 0;
    M5GFX display;
    UiRenderer renderer(display);
    if (!renderer.begin(previewColorDepth())) return 1;
    UiModel model; model.tick(1000); model.setBattery(full_battery ? 100 : 72,false,true);
    UiData data{}; data.settings = defaultSettings(); data.revision = 1;
    data.recording.state = unprepared ? RecordingState::Stopped : paused ? RecordingState::Paused : RecordingState::Recording;
    data.recording.elapsed_ms = unprepared ? 0 : (3*3600+42*60+18)*1000;
    data.storage.card_state = CardState::Mounted;
    data.storage.rg = {1,1,unprepared ? RgCacheState::Unprepared : rebuilding ? RgCacheState::Rebuilding : RgCacheState::Ready};
    data.storage.sd = {1,1,io};
    data.storage.log.buffered = sd_error ? 64 : paused || unprepared ? 0 : 2;
    data.storage.log.error = sd_error ? LogError::Io : LogError::None;
    data.gps.reception = GpsReception::Fix;
    data.gps.fix = {35.681641,140.027194,19921000,true}; // JST 14:32:01.
    data.gps.fix.fields = GpsSpeed|GpsCourse|GpsAltitude|GpsSatellites|GpsHdop;
    data.gps.fix.speed_kmh = long_speed ? 99.9f : 4.2f;
    if (!std::strcmp(mode,"speed-sub-one")) data.gps.fix.speed_kmh=0.42f;
    if (!std::strcmp(mode,"speed-integer")) data.gps.fix.speed_kmh=999;
    if (!std::strcmp(mode,"speed-one")) data.gps.fix.speed_kmh=1;
    if (!std::strcmp(mode,"speed-ten")) data.gps.fix.speed_kmh=10;
    if (!std::strcmp(mode,"speed-hundred")) data.gps.fix.speed_kmh=100;
    if (!std::strcmp(mode,"speed-round-ten")) data.gps.fix.speed_kmh=9.996f;
    if (!std::strcmp(mode,"speed-round-hundred")) data.gps.fix.speed_kmh=99.96f;
    data.gps.fix.course_deg = 140; data.gps.fix.altitude_m = 12.3;
    data.gps.fix.satellites = 12; data.gps.fix.hdop = 0.8;
    if (missing) data.gps.fix.fields = 0;
    if (!std::strcmp(mode,"bounds")) { data.gps.fix.latitude=-90; data.gps.fix.longitude=-180; data.gps.fix.altitude_m=-2147483.5f; }
    if (!std::strcmp(mode,"heading-wrap")) data.gps.fix.course_deg=359.6f;
    if (!std::strcmp(mode,"overflow")) data.gps.fix.speed_kmh=1000;
    if (!std::strcmp(mode,"zero")) { data.gps.fix.speed_kmh=0; data.gps.fix.course_deg=0; data.gps.fix.altitude_m=0; data.gps.fix.satellites=0; }
    data.place_fix = data.gps.fix;
    data.place_state = unprepared ? PlaceState::Searching : PlaceState::Ready;
    if (!unprepared) {
        PlaceResult place{}; place.request.fix=data.gps.fix; place.state=PlaceState::Ready;
        std::vector<std::string> parts{"千葉県","","習志野市","","鷺沼","2丁目1番1号"};
        if (!std::strcmp(mode,"n03")) parts={"千葉県","","","習志野市",""};
        if (!std::strcmp(mode,"long-address")) parts[2]="あいうえおかきくけこさしすせ";
        if (!std::strcmp(mode,"space-width")) parts={"日 日"};
        if (!std::strcmp(mode,"space-boundary")) parts={"日日日日日日日日日日日日日日 "};
        if (!std::strcmp(mode,"empty-address")) formatPlaceLines(parts,place.lines,PlaceDisplayFormat{{"",""},true});
        else formatPlaceLines(parts,place.lines);
        completePlacePresentation(data,place);
    }
    model.setScreen({ScreenId::LivePlace,DisplayMode::View,1,data,0,ActionResult::None});
    // Let the preceding access pulse expire, then display the fixture state.
    model.tick(1200);
    if (gps_error) {
        data.gps.fix = {}; data.gps.reception = GpsReception::NoData;
        std::memset(data.place,0,sizeof(data.place)); data.place_fix = {}; data.place_state = PlaceState::NoFix;
        ++data.revision; model.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    }
    renderer.draw(model);
    auto check_pixel = [&](int x,int y,uint16_t color) {
        if (display.pixels[y*240+x] != color) {
            char error[96]; std::snprintf(error,sizeof(error),"unexpected pixel %d,%d: %04x != %04x",x,y,display.pixels[y*240+x],color);
            throw std::runtime_error(error);
        }
    };
    if (PixelFont::width("日 日",TextRole::Place)!=40 || PixelFont::width("A A",TextRole::Place)!=40 ||
        PixelFont::width("日　日",TextRole::Place)!=48 || PixelFont::width("A A",TextRole::UiLarge)!=48)
        throw std::runtime_error("place space width must be eight; other 16-dot roles and full-width spaces stay unchanged");
    if (!std::strcmp(mode,"space-width")) {
        const auto* day=shinonomeGlyph(U'日');
        for(int y=0;y<16;++y)for(int x=0;x<16;++x)
            check_pixel(28+x,20+y,day[y] & (0x8000u>>x)?TFT_WHITE:TFT_BLACK);
        for(int y=20;y<36;++y)for(int x=20;x<28;++x)check_pixel(x,y,TFT_BLACK);
    }
    if (!std::strcmp(mode,"empty-address"))
        for(int y=20;y<56;++y)for(int x=4;x<236;++x)check_pixel(x,y,TFT_BLACK);
    for(int y=20;y<56;++y)for(int x=236;x<240;++x)check_pixel(x,y,TFT_BLACK);
    // These independent region checks cover all scenes, including long numbers.
    for (int y=60;y<=106;++y) check_pixel(120,y,TFT_WHITE);
    for (int x=0;x<240;++x) { check_pixel(x,16,TFT_WHITE); check_pixel(x,60,TFT_WHITE); check_pixel(x,106,TFT_WHITE); check_pixel(x,120,TFT_WHITE); }
    for (int x=0;x<=120;++x) check_pixel(x,83,TFT_WHITE);
    for (int x=121;x<240;++x) check_pixel(x,83,TFT_BLACK);
    for (int x=236;x<240;++x) for (int y=65;y<103;++y) check_pixel(x,y,TFT_BLACK);
    if (!unprepared && !gps_error && !missing && std::strcmp(mode,"heading-wrap") && std::strcmp(mode,"zero")) {
        const auto* degree=shinonomeGlyph(U'°');
        for (int y=0;y<16;++y) for (int x=0;x<16;++x)
            check_pixel(84+x,89+y,degree[y] & (0x8000u>>x) ? TFT_WHITE : TFT_BLACK);
    }
    // The left labels stay in the upper-left corner of each equal-height row.
    auto check_ascii = [&](int x,int y,const char* value,uint16_t color=TFT_WHITE) {
        for (const auto* p=value;*p;++p,x+=8)
            for(int row=0;row<8;++row) for(int col=0;col<8;++col)
                check_pixel(x+col,y+row,font8x8_c64[(*p-32)*8+col] & (1u<<row) ? color : TFT_BLACK);
    };
    const bool recovery=placeIssue(data) != PlaceState::Ready && loggerStopped(data.recording,data.storage.log);
    check_ascii(4,124,recovery?"ESC MAINT":"P||",recovery?TFT_CYAN:data.recording.state == RecordingState::Recording ? TFT_WHITE : TFT_DARKGREY);
    check_ascii(4,62,"SPD"); check_ascii(4,85,"HDG");
    if(recovery)check_ascii(92,124,"2:GPS 3:LOG 4:MAP",TFT_LIGHTGREY);
    else {
        check_ascii(36,124,"2",TFT_CYAN);check_ascii(44,124,":GPS",TFT_LIGHTGREY);
        check_ascii(84,124,"3",TFT_CYAN);check_ascii(92,124,":LOG",TFT_LIGHTGREY);
        check_ascii(132,124,"4",TFT_CYAN);check_ascii(140,124,":MAP",TFT_LIGHTGREY);
        check_ascii(180,124,"ESC",TFT_CYAN);check_ascii(204,124,":SET",TFT_LIGHTGREY);
    }
    check_ascii(84,73,"km/h");
    const auto speed_detail=formatLiveGps(model.data().gps,model.data().place_fix);
    if(speedTextWidth(speed_detail.speed)>56) throw std::runtime_error("speed exceeded its three-digit viewport");
    const int speed_left=84-speedTextWidth(speed_detail.speed);
    if(speed_left<28) throw std::runtime_error("speed overlaps the SPD label");
    // All digits use native 16-dot glyphs; only the decimal point has an 8-dot advance.
    if(!std::strcmp(mode,"speed-one") && std::strcmp(speed_detail.speed,"1.00")) throw std::runtime_error("one km/h precision");
    if(!std::strcmp(mode,"speed-ten") && std::strcmp(speed_detail.speed,"10.0")) throw std::runtime_error("ten km/h precision");
    if(!std::strcmp(mode,"speed-hundred") && std::strcmp(speed_detail.speed,"100")) throw std::runtime_error("hundred km/h precision");
    if(!std::strcmp(mode,"speed-sub-one") && std::strcmp(speed_detail.speed,"0")) throw std::runtime_error("sub-one km/h display");
    if (!std::strcmp(mode,"n03"))
        for(int y=40;y<56;++y) for(int x=4;x<228;++x) check_pixel(x,y,TFT_BLACK);
    if (full_battery)
        for (int col=0;col<32;++col) for (int row=0;row<8;++row) {
            const char* value="100%";
            const bool lit=font8x8_c64[(value[col/8]-32)*8+col%8] & (1u<<row);
            check_pixel(204+col,4+row,lit ? TFT_WHITE : TFT_BLACK);
        }
    std::printf("P6\n240 135\n255\n");
    for (const auto pixel : display.pixels) {
        const unsigned red=(pixel>>11)&31, green=(pixel>>5)&63, blue=pixel&31;
        std::putchar((red<<3)|(red>>2)); std::putchar((green<<2)|(green>>4)); std::putchar((blue<<3)|(blue>>2));
    }
}
'''


def main():
    enum_source = (M5GFX / "v1/misc/enum.hpp").read_text()
    renderer_source = (ROOT / "application/task/ui/UiRenderer.cpp").read_text() + (ROOT / "application/task/ui/UiCanvas.h").read_text()
    names = sorted(set(re.findall(r"\bTFT_[A-Z]+\b", SCENE + renderer_source)))
    colors = []
    for name in names:
        value = re.search(rf"\b{re.escape(name)}\s*=\s*(0x[0-9A-Fa-f]+)", enum_source)
        if value is None:
            raise SystemExit(f"Missing M5GFX color {name}")
        colors.append(f"inline constexpr uint16_t {name} = {value[1]};")
    with tempfile.TemporaryDirectory(prefix="geoputer-layout-") as directory:
        work = Path(directory)
        (work / "M5GFX.h").write_text(ADAPTER + "\n" + "\n".join(colors))
        (work / "scene.cpp").write_text(SCENE)
        executable = work / "render"
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(work), "-I", str(ROOT / "application"), "-I", str(ROOT / "tests/support"),
            "-I", str(M5GFX / "Fonts"), str(work / "scene.cpp"),
            str(ROOT / "application/task/ui/PixelFont.cpp"),
            str(ROOT / "application/task/ui/UiRenderer.cpp"),
            str(ROOT / "application/task/ui/UsbRenderer.cpp"),
            str(ROOT / "application/task/ui/UiModel.cpp"),
            str(ROOT / "application/task/ui/LicenseText.cpp"),
            str(ROOT / "application/task/ui/SettingsText.cpp"),
            str(ROOT / "application/Settings.cpp"),
            str(ROOT / "application/task/storage/galuchat/TextEncoding.cpp"),
            str(ROOT / "application/task/ui/Shinonome.cpp"),
            "-o", str(executable),
        ], check=True, timeout=180)
        image_modes = ("rec", "pause", "sd-error", "gps-error", "reload", "n03", "sd-write", "sd-read", "dictionary-read", "unprepared")
        verification_modes = ("speed-long", "speed-sub-one", "speed-integer", "speed-one", "speed-ten", "speed-hundred", "speed-round-ten", "speed-round-hundred", "full-battery", "bounds", "missing", "heading-wrap", "overflow", "zero", "long-address", "space-width", "space-boundary", "empty-address")
        for mode in image_modes + verification_modes:
            ppm = work / f"{mode}.ppm"
            with ppm.open("wb") as output:
                output.write(reference_frame(executable, mode))
            with Image.open(ppm) as source:
                image = source.convert("RGB")
            assert image.size == (240, 135)
            assert len(image.getcolors(256)) <= 9
            assert image.getpixel((124, 2)) == ((0, 255, 0) if mode not in ("reload", "unprepared") else (255, 255, 0) if mode == "reload" else (123, 125, 123))
            if mode in ("sd-read", "sd-write", "reload", "dictionary-read"):
                assert image.getpixel((152, 2)) == (0, 0, 255)
            if mode in ("sd-error", "gps-error"):
                assert image.getpixel((0, 107)) == (255, 0, 0)
                assert image.getpixel((0, 119)) == (255, 0, 0)
            if mode in verification_modes:
                print(f"Verified {mode}: numeric bounds, native glyphs and panel boundaries")
                continue
            stem = f"live-place-refined-{mode}"
            image.save(OUTPUT / f"{stem}.png")
            image.resize((960, 540), Image.Resampling.NEAREST).save(OUTPUT / f"{stem}-preview.png")
            print(f"Created {stem}.png: 240x135, native pixel fonts, 4x nearest-neighbor preview")


if __name__ == "__main__":
    main()

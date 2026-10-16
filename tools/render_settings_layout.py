"""Render bilingual Settings using the production renderer and native bitmap fonts."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
from PIL import Image
from ui_preview_support import ROOT, M5GFX, ADAPTER

SCENE = r'''
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "task/ui/UiRenderer.h"
#include "task/ui/SettingsLayout.h"
#include "task/ui/SettingsText.h"
#include "task/ui/Shinonome.h"
#include "task/ui/LicenseText.h"
using namespace geoputer;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int width(const char* text,bool english){
    int result=0;
    for(const auto* p=reinterpret_cast<const unsigned char*>(text);*p;){
        const unsigned length=*p<128?1:*p<224?2:*p<240?3:4;
        result+=english&&*p<128?8:16;p+=length;
    }
    return result;
}
int main(int argc,char** argv){
    check(argc==3,"usage: scene language");
    const char* mode=argv[1];const bool english=!std::strcmp(argv[2],"en");
    const auto language=english?UiLanguage::English:UiLanguage::Japanese;
    for(unsigned n=0;n<4;++n){
        check(width(settingsRootLabel(language,n),english)<=216,"root label exceeds screen");
        check(width(settingsStorageLabel(language,n),english)<=216,"storage label exceeds screen");
    }
    for(unsigned n=0;n<SettingCount;++n){
        for(unsigned choice=0;choice<settingChoices(n);++choice){
            char value[32];settingsValueText(language,n,choice,value,sizeof(value));
            const int value_x=236-width(value,english||n==0);
            check(20+width(settingsOperationLabel(language,n),english)+8<=value_x,"setting label overlaps right-aligned value");
        }
    }
    for(auto label:{SettingsLabel::FormatQuestion,SettingsLabel::ResetQuestion,SettingsLabel::FormatScope,
                    SettingsLabel::ResetScope,SettingsLabel::KeepSd})
        check(width(settingsLabel(language,label),english)<=232,"confirmation text exceeds screen");
    check(width(settingsLabel(language,SettingsLabel::ResetAction),english)<=216,"reset action exceeds screen");
    M5GFX display;UiRenderer renderer(display);check(renderer.begin(),"renderer init");
    UiModel ui;UiData data{};data.settings=defaultSettings();data.settings.language=language;
    if(!std::strcmp(mode,"timeout-never"))data.settings.screen_timeout=4;
    data.storage.settings_state=SettingsState::Defaults;
    if(std::strcmp(mode,"license-baseline")){
        data.storage.card_state=CardState::Mounted;data.storage.fat_type=32;
        data.storage.card_bytes=16ull<<30;data.storage.total_bytes=15ull<<30;data.storage.free_bytes=12ull<<30;
    }
    ui.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    auto key=[&](UiKey k){ui.keyPressed(k,1);};
    if(!std::strcmp(mode,"operation")||!std::strcmp(mode,"operation-bottom")||!std::strcmp(mode,"language-edit")||!std::strcmp(mode,"timeout-never")){
        key(UiKey::Enter);
        if(!std::strcmp(mode,"operation-bottom"))for(unsigned n=0;n<6;++n)key(UiKey::Down);
        if(!std::strcmp(mode,"language-edit")){key(UiKey::Enter);key(UiKey::Right);}
        if(!std::strcmp(mode,"timeout-never"))for(unsigned n=0;n<3;++n)key(UiKey::Down);
    }else if(!std::strcmp(mode,"storage")||!std::strcmp(mode,"card")||!std::strcmp(mode,"format-confirm")){
        key(UiKey::Down);key(UiKey::Enter);
        if(!std::strcmp(mode,"card"))key(UiKey::Enter);
        if(!std::strcmp(mode,"format-confirm")){for(unsigned n=0;n<3;++n)key(UiKey::Down);key(UiKey::Enter);}
    }else if(!std::strcmp(mode,"reset")||!std::strcmp(mode,"reset-confirm")){
        key(UiKey::Down);key(UiKey::Down);key(UiKey::Enter);
        if(!std::strcmp(mode,"reset-confirm"))key(UiKey::Enter);
    }else if(!std::strcmp(mode,"licenses")||!std::strcmp(mode,"license-baseline")){
        for(unsigned n=0;n<3;++n)key(UiKey::Down);
        key(UiKey::Enter);
    }
    if(!std::strcmp(mode,"license-baseline")){
        const unsigned last=licenseLineCount()-SettingsLayout::LicenseRows;
        for(unsigned n=0;n<=last;++n){
            if(n==0||n==17||n==last){renderer.draw(ui);std::fwrite(display.pixels.data(),2,display.pixels.size(),stdout);}
            key(UiKey::Down);
        }
        return 0;
    }
    if(!std::strcmp(mode,"operation")) {
        auto verifyText=[&](int x,int y,const char* text,bool ascii16,uint16_t color) {
            while(*text) {
                const auto first=static_cast<unsigned char>(*text++);
                uint32_t cp=first;unsigned remaining=0;
                if(first>=0xe0){cp=first&15;remaining=2;}else if(first>=0xc0){cp=first&31;remaining=1;}
                while(remaining--)cp=(cp<<6)|(static_cast<unsigned char>(*text++)&63);
                const bool narrow=ascii16 && first<128;
                for(int row=0;row<16;++row)for(int col=0;col<(narrow?8:16);++col) {
                    const bool ink=narrow?(FontLib8x16[cp*16+row]&(0x80u>>col)):(shinonomeGlyph(cp)[row]&(0x8000u>>col));
                    check(display.pixels.at((y+row)*240+x+col)==(ink?color:TFT_BLACK),"operation label/value alignment or glyph mismatch");
                }
                x+=narrow?8:16;
            }
        };
        for(unsigned row=0;row<SettingCount;++row)for(unsigned choice=0;choice<settingChoices(row);++choice) {
            UiData trial=data;
            check(setSettingValue(trial.settings,row,choice),"setting choice fixture");
            trial.settings.language=language;
            UiModel model;model.setScreen({ScreenId::Settings,DisplayMode::View,1,trial,0,ActionResult::None});
            model.keyPressed(UiKey::Enter,1);
            for(unsigned n=0;n<row;++n)model.keyPressed(UiKey::Down,n+2);
            if(!row) {
                model.keyPressed(UiKey::Enter,10);
                if(choice!=static_cast<unsigned>(language))model.keyPressed(UiKey::Right,11);
            }
            renderer.draw(model);
            const auto color=settingEditable(row)?TFT_CYAN:TFT_DARKGREY;
            const int y=27+(row-SettingsLayout::firstVisible(row,SettingCount))*20;
            verifyText(20,y,settingsOperationLabel(language,row),english,color);
            char value[32];settingsValueText(language,row,choice,value,sizeof(value));
            verifyText(236-width(value,english||!row),y,value,english||!row,color);
            for(int yy=y;yy<y+16;++yy)for(int x=236;x<240;++x)
                check(display.pixels.at(yy*240+x)==TFT_BLACK,"value crossed the right margin");
        }
    }
    renderer.draw(ui);
    auto pixel=[&](int x,int y,uint16_t color){check(display.pixels.at(y*240+x)==color,"native font pixel mismatch");};
    auto ascii=[&](int x,int y,char c,uint16_t color){
        for(int row=0;row<16;++row)for(int col=0;col<8;++col)
            pixel(x+col,y+row,FontLib8x16[static_cast<unsigned char>(c)*16+row]&(0x80u>>col)?color:TFT_BLACK);
    };
    auto japanese=[&](int x,int y,char32_t c,uint16_t color){
        const auto* rows=shinonomeGlyph(c);
        for(int row=0;row<16;++row)for(int col=0;col<16;++col)
            pixel(x+col,y+row,rows[row]&(0x8000u>>col)?color:TFT_BLACK);
    };
    if(!std::strcmp(mode,"root")){
        if(english)ascii(20,47,'S',TFT_WHITE);else japanese(20,27,U'動',TFT_CYAN);
    }
    if(!std::strcmp(mode,"operation")){
        if(english)ascii(20,27,'L',TFT_CYAN);else japanese(20,27,U'表',TFT_CYAN);
    }
    if(!std::strcmp(mode,"language-edit")){
        if(english)japanese(188,27,U'日',TFT_CYAN);else ascii(180,27,'E',TFT_CYAN);
    }
    if(!std::strcmp(mode,"timeout-never")){
        if(english)ascii(196,87,'N',TFT_CYAN);else{
            japanese(156,87,U'消',TFT_CYAN);japanese(220,87,U'い',TFT_CYAN);
        }
    }
    for(int x=0;x<240;++x){pixel(x,22,TFT_DARKGREY);pixel(x,118,TFT_DARKGREY);}
    std::printf("P6\n240 135\n255\n");
    for(auto color:display.pixels){
        const unsigned char rgb[]={static_cast<unsigned char>(((color>>11)&31)*255/31),
            static_cast<unsigned char>(((color>>5)&63)*255/63),static_cast<unsigned char>((color&31)*255/31)};
        std::fwrite(rgb,1,3,stdout);
    }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--license-baseline", type=Path, help="Compare license frames captured before this change")
    args = parser.parse_args()
    output = ROOT / "doc/画面・操作/settings"
    enum_source = (M5GFX / "v1/misc/enum.hpp").read_text()
    renderer = (ROOT / "application/task/ui/UiRenderer.cpp").read_text()
    colors = []
    for name in sorted(set(re.findall(r"\bTFT_[A-Z]+\b", SCENE + renderer))):
        value = re.search(rf"\b{name}\s*=\s*(0x[0-9A-Fa-f]+)", enum_source)[1]
        colors.append(f"inline constexpr uint16_t {name}={value};")
    modes = ("root", "operation", "operation-bottom", "language-edit", "timeout-never", "storage", "card", "reset", "reset-confirm", "format-confirm", "licenses")
    with tempfile.TemporaryDirectory(prefix="geoputer-settings-") as directory:
        work = Path(directory)
        (work / "M5GFX.h").write_text(ADAPTER + "\n" + "\n".join(colors))
        (work / "scene.cpp").write_text(SCENE)
        source = [ROOT / "application/task/ui" / f"{name}.cpp" for name in
                  ("PixelFont", "UiRenderer", "UiModel", "LicenseText", "SettingsText", "Shinonome")]
        executable = work / "render"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fsanitize=undefined",
                        "-fno-sanitize-recover=all", "-I", str(work), "-I", str(ROOT / "application"),
                        "-I", str(ROOT / "tests/support"), "-I", str(M5GFX / "Fonts"), str(work / "scene.cpp"),
                        *map(str, source), str(ROOT / "application/Settings.cpp"), "-o", str(executable)], check=True, timeout=60)
        license_frames = []
        for language in ("ja", "en"):
            license_frames.append(subprocess.check_output([str(executable), "license-baseline", language], timeout=20))
            for mode in modes:
                ppm = work / "screen.ppm"
                with ppm.open("wb") as target:
                    subprocess.run([str(executable), mode, language], stdout=target, check=True, timeout=10)
                with Image.open(ppm) as source_image:
                    image = source_image.convert("RGB")
                stem = f"settings-{language}-{mode}"
                image.save(output / f"{stem}.png")
                image.resize((960, 540), Image.Resampling.NEAREST).save(output / f"{stem}-preview.png")
            print(f"PASS: {language}, {len(modes)} scenes, native font pixels and text bounds")
        assert license_frames[0] == license_frames[1], "Language changes license framebuffers"
        if args.license_baseline:
            assert license_frames[0] == args.license_baseline.read_bytes(), "License display differs from the original"
        print("PASS: license framebuffers are unchanged across languages" + (" and match the original" if args.license_baseline else ""))


if __name__ == "__main__":
    main()

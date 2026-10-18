"""Render dataset rejection warnings using the production renderer at native resolution."""
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
#include "Config.h"
using namespace geoputer;
int main(int argc, char** argv) {
    if (argc != 3) return 1;
    const bool english = !std::strcmp(argv[1], "en");
    const bool memory = !std::strcmp(argv[2], "memory");
    UiData data{};
    data.settings = defaultSettings();
    data.settings.language = english ? UiLanguage::English : UiLanguage::Japanese;
    data.storage.card_state = CardState::Mounted;
    data.dataset = {memory ? PlaceState::OutOfMemory : PlaceState::TooManyChunks, 2000, config::MaxDatasetChunks};
    data.place_state = data.dataset.state;
    M5GFX display;
    UiRenderer renderer(display);
    if (!renderer.begin(previewColorDepth())) return 2;
    UiModel model;
    model.setScreen({ScreenId::LivePlace, DisplayMode::View, 1, data, 0, ActionResult::None});
    if (!model.datasetWarning()) return 3;
    renderer.draw(model);
    // No text may cross the four-dot right margin in the warning body.
    for (int y=24; y<116; ++y) for (int x=236; x<240; ++x)
        if (display.pixels.at(y*240+x) != TFT_BLACK) throw std::runtime_error("warning overflow");
    std::printf("P6\n240 135\n255\n");
    for (auto color : display.pixels) {
        const unsigned char rgb[] = {static_cast<unsigned char>(((color>>11)&31)*255/31),
            static_cast<unsigned char>(((color>>5)&63)*255/63), static_cast<unsigned char>((color&31)*255/31)};
        std::fwrite(rgb, 1, 3, stdout);
    }
}
'''


def main():
    enum_source = (M5GFX / "v1/misc/enum.hpp").read_text()
    renderer = (ROOT / "application/task/ui/UiRenderer.cpp").read_text() + (ROOT / "application/task/ui/UiCanvas.h").read_text()
    colors = []
    for name in sorted(set(re.findall(r"\bTFT_[A-Z]+\b", SCENE + renderer))):
        value = re.search(rf"\b{name}\s*=\s*(0x[0-9A-Fa-f]+)", enum_source)[1]
        colors.append(f"inline constexpr uint16_t {name}={value};")
    output = ROOT / "doc/画面・操作/settings"
    with tempfile.TemporaryDirectory(prefix="geoputer-warning-") as directory:
        work = Path(directory)
        (work / "M5GFX.h").write_text(ADAPTER + "\n" + "\n".join(colors))
        (work / "scene.cpp").write_text(SCENE)
        sources = [ROOT / "application/task/ui" / f"{name}.cpp" for name in
                   ("PixelFont", "UiRenderer", "UsbRenderer", "UiModel", "LicenseText", "SettingsText", "Shinonome")]
        executable = work / "render"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(work),
                        "-I", str(ROOT / "application"), "-I", str(ROOT / "tests/support"),
                        "-I", str(M5GFX / "Fonts"), str(work / "scene.cpp"), *map(str, sources),
                        str(ROOT / "application/Settings.cpp"), "-o", str(executable)], check=True, timeout=180)
        for language in ("ja", "en"):
            for kind in ("limit", "memory"):
                ppm = work / "screen.ppm"
                with ppm.open("wb") as target:
                    target.write(reference_frame(executable, language, kind))
                with Image.open(ppm) as source:
                    image = source.convert("RGB")
                stem = f"dataset-{kind}-{language}"
                image.save(output / f"{stem}.png")
                image.resize((960, 540), Image.Resampling.NEAREST).save(output / f"{stem}-preview.png")
        print("PASS: bilingual dataset limit/memory warnings, native 240x135 and text bounds")


if __name__ == "__main__":
    main()

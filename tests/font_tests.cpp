#include <cstring>
#include <stdexcept>
#include "task/ui/Shinonome.h"
#include "task/ui/LicenseText.h"
#include "task/ui/SettingsLayout.h"
#include "task/ui/UiModel.h"

using namespace geoputer;
namespace {
void check(bool value, const char* detail) { if (!value) throw std::runtime_error(detail); }
}

void testFonts() {
    const uint16_t native_a[16] = {0x0000, 0x0080, 0x0080, 0x0140, 0x0140, 0x0220, 0x0220, 0x0410,
                                   0x07F0, 0x0410, 0x0808, 0x0808, 0x1004, 0x380E, 0x0000, 0x0000};
    const uint16_t native_day[16] = {0x0000, 0x0FF8, 0x0808, 0x0808, 0x0808, 0x0808, 0x0808, 0x0FF8,
                                     0x0808, 0x0808, 0x0808, 0x0808, 0x0808, 0x0808, 0x0FF8, 0x0000};
    check(std::memcmp(shinonomeGlyph('A'), native_a, sizeof(native_a)) == 0 &&
          std::memcmp(shinonomeGlyph(U'日'), native_day, sizeof(native_day)) == 0,
          "ASCII and Japanese use original Shinonome 16-dot bitmaps rather than scaled 8-dot glyphs");
    check(std::memcmp(shinonomeGlyph('A'), shinonomeGlyph(U'Ａ'), sizeof(native_a)) == 0,
          "ASCII selects the native full-width letter at the same weight as Japanese");
    for (const auto character : U"動作設定ストレージ操作初期化ライセンス情報カード再マウント安全な取り外しでフォーマット千葉県習志野市鷺沼使用できませんチャンク数上限メモリ不足構成不正形式未対応地図読込用文字コード見つかりません") {
        if (character) check(shinonomeGlyph(character) != shinonomeGlyph(U'□'), "required menu/place glyph is available");
    }
    check(shinonomeGlyph(U'°') != shinonomeGlyph(U'□'), "native degree glyph exists for large HDG");
    for (unsigned row=0;row<16;++row)
        check(!(shinonomeGlyph('.')[row] & 0x03ffu), "native decimal fits its compact eight-dot advance");
    for (uint32_t code = 0x21; code <= 0x7E; ++code)
        check(shinonomeGlyph(code) != shinonomeGlyph(U'□'), "all printable ASCII has a native large-font glyph");
    check(shinonomeGlyph(0x1F680) == shinonomeGlyph(U'□') && shinonomeGlyph(0xFFFFFFFF) == shinonomeGlyph(U'□'),
          "uncovered Unicode falls back to a bounded glyph from the same family");
    for (size_t row = 0; row < licenseLineCount(); ++row) {
        const auto* line = licenseLine(row);
        check(std::strlen(line) <= SettingsLayout::LicenseColumns, "license lines fit the 29-column 8-dot viewport");
        for (const auto* p = line; *p; ++p)
            check(static_cast<unsigned char>(*p) >= 0x20 && static_cast<unsigned char>(*p) <= 0x7E,
                  "license body is entirely printable ASCII");
    }
    UiModel ui;
    SetUiScreen command{};
    command.screen = ScreenId::Settings;
    command.mode = DisplayMode::View;
    command.generation = 1;
    command.data.settings = defaultSettings();
    ui.setScreen(command);
    for (unsigned i = 0; i < settingsRootIndex(SettingsRootItem::Licenses); ++i) ui.keyPressed(UiKey::Down, 1 + i);
    ui.keyPressed(UiKey::Enter, 4);
    for (size_t i = 0; i < licenseLineCount() + 10; ++i) ui.keyPressed(UiKey::Down, 5 + i);
    check(ui.licenseOffset() + SettingsLayout::LicenseRows == licenseLineCount(),
          "scrolling clamps exactly at the last nine-row license page");
    ui.keyPressed(UiKey::Up, 10000);
    check(ui.licenseOffset() + SettingsLayout::LicenseRows + 1 == licenseLineCount(),
          "license can scroll back from the final page");
}

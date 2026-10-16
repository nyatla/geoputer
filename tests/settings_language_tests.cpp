#include <cstring>
#include <stdexcept>
#include "Settings.h"
#include "task/storage/SettingsStore.h"
#include "task/ui/SettingsText.h"
#include "task/ui/UiModel.h"
#include "task/ui/Shinonome.h"

using namespace geoputer;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void updateChecksum(uint8_t (&record)[16]) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 12; ++i) hash = (hash ^ record[i]) * 16777619u;
    for (unsigned i = 0; i < 4; ++i) record[12+i] = static_cast<uint8_t>(hash >> (8*i));
}
}

void testSettingsLanguage() {
    check(defaultSettings().language == UiLanguage::Japanese, "new settings default to Japanese");
    const uint8_t legacy[] = {0x47,0x50,0x01,0x06,0x02,0x04,0x03,0x04,0x02,0x00,0x00,0x00,0x26,0xcf,0x63,0x6b};
    Settings restored{};
    check(decodeSettings(legacy, sizeof(legacy), restored) && restored.language == UiLanguage::Japanese &&
          restored.log_interval == 2 && restored.flush_interval == 4 && restored.screen_timeout == 3 &&
          restored.brightness == 4 && restored.dataset == 2 && restored.gps_mode == 0,
          "v1 NVS fixture restores all six original fields and adds Japanese without resetting settings");
    Preferences::records.clear();
    Preferences::records["geoputer/config"] = {legacy, legacy + sizeof(legacy)};
    SettingsStore store;
    check(store.begin() && store.load(restored) == SettingsState::Loaded, "existing NVS record is accepted");
    restored.language = UiLanguage::English;
    check(store.save(restored), "language selection saves a v2 NVS record");
    SettingsStore rebooted; Settings after_reboot{};
    check(rebooted.begin() && rebooted.load(after_reboot) == SettingsState::Loaded && sameSettings(restored, after_reboot),
          "reboot retains the selected language and original settings");
    check(Preferences::records["geoputer/config"][2] == 2 && Preferences::records["geoputer/config"][3] == SettingCount,
          "saved record carries the new version and field count");
    uint8_t record[16]; encodeSettings(restored, record);
    record[10] = 2; updateChecksum(record);
    check(!decodeSettings(record, sizeof(record), after_reboot) && sameSettings(after_reboot, restored),
          "invalid language is rejected even with a valid checksum, without changing active settings");
    encodeSettings(restored, record); record[2] = 3; updateChecksum(record);
    check(!decodeSettings(record, sizeof(record), after_reboot), "unsupported future format is rejected");
    encodeSettings(restored, record); record[11] = 1; updateChecksum(record);
    check(!decodeSettings(record, sizeof(record), after_reboot), "reserved bytes remain validated");

    UiModel ui; UiData data{}; data.settings = defaultSettings();
    ui.setScreen({ScreenId::Settings, DisplayMode::View, 1, data, 0, ActionResult::None});
    ui.keyPressed(UiKey::Enter, 1); ui.keyPressed(UiKey::Enter, 2); ui.keyPressed(UiKey::Right, 3);
    check(ui.editValue() == 1 && ui.data().settings.language == UiLanguage::Japanese, "language preview does not apply before confirmation");
    ui.keyPressed(UiKey::Escape, 4);
    check(ui.data().settings.language == UiLanguage::Japanese && ui.snapshot().action.kind == ActionKind::None,
          "cancelling language edit preserves the active language");
    ui.keyPressed(UiKey::Enter, 5); ui.keyPressed(UiKey::Right, 6); ui.keyPressed(UiKey::Enter, 7);
    const auto action = ui.snapshot().action;
    check(action.kind == ActionKind::SaveSettings && action.settings.language == UiLanguage::English,
          "confirmed Language change is a normal App-owned settings save");
    data.settings = action.settings; ++data.revision;
    ui.updateContent({DisplayMode::View, 1, data, action.action_id, ActionResult::Applied});
    check(ui.data().settings.language == UiLanguage::English && ui.snapshot().selected_setting == 0 &&
          ui.snapshot().page == SettingsPage::Operation && ui.snapshot().action.kind == ActionKind::None,
          "App acknowledgement applies language immediately without losing selection or navigating away");
    for (unsigned n=0;n<SettingCount;++n) ui.keyPressed(UiKey::Down, 10+n);
    ui.keyPressed(UiKey::Enter, 30);
    check(!ui.editing() && ui.snapshot().selected_setting == 6, "GPS remains read-only after adding Language");
    ui.keyPressed(UiKey::Up, 31); ui.keyPressed(UiKey::Enter, 32);
    check(!ui.editing(), "dataset remains read-only");

    char text[32];
    settingsValueText(UiLanguage::Japanese, 1, 2, text, sizeof(text));
    check(std::strcmp(text,"10秒") == 0, "Japanese intervals use localized units");
    settingsValueText(UiLanguage::English, 1, 2, text, sizeof(text));
    check(std::strcmp(text,"10 s") == 0, "English intervals use ASCII units");
    settingsValueText(UiLanguage::Japanese, 3, 4, text, sizeof(text));
    check(std::strcmp(text,"消灯しない") == 0, "Japanese never-sleep choice is localized");
    settingsValueText(UiLanguage::English, 3, 4, text, sizeof(text));
    check(std::strcmp(text,"Never") == 0, "English never-sleep choice is localized");
    check(std::strcmp(settingsOperationLabel(UiLanguage::Japanese, 0), "表示言語") == 0 &&
          std::strcmp(settingsOperationLabel(UiLanguage::English, 0), "Language") == 0,
          "Language is the first operation setting in both languages");
    check(std::strcmp(settingsMessage(UiLanguage::Japanese,"SETTINGS SAVED"),"設定を保存しました") == 0 &&
          std::strcmp(settingsMessage(UiLanguage::English,"SETTINGS SAVED"),"SETTINGS SAVED") == 0,
          "settings completion messages follow the active language");
    // All Japanese menu/confirmation characters must exist in the current font.
    for (unsigned group=0;group<4;++group) for (unsigned row=0;row<(group==1?SettingCount:group==3?14u:4u);++row) {
        const char* label = group==0?settingsRootLabel(UiLanguage::Japanese,row):
            group==1?settingsOperationLabel(UiLanguage::Japanese,row):
            group==2?settingsStorageLabel(UiLanguage::Japanese,row):settingsLabel(UiLanguage::Japanese,static_cast<SettingsLabel>(row));
        while (*label) {
            const auto c=static_cast<unsigned char>(*label++); uint32_t cp=c; unsigned remaining=0;
            if(c>=0xe0){cp=c&15;remaining=2;} else if(c>=0xc0){cp=c&31;remaining=1;}
            while(remaining--) cp=(cp<<6)|(static_cast<unsigned char>(*label++)&63);
            check(shinonomeGlyph(cp)!=shinonomeGlyph(U'□'), "localized menu/confirmation glyph is present in Shinonome");
        }
    }
}

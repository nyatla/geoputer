#include "SettingsText.h"
#include <cstdio>
#include <cstring>

namespace geoputer {
namespace {
struct Translation { const char* japanese; const char* english; };
const char* select(UiLanguage language, const Translation& text) {
    return language == UiLanguage::English ? text.english : text.japanese;
}
template <size_t N>
const char* at(UiLanguage language, const Translation (&table)[N], unsigned row) {
    return row < N ? select(language, table[row]) : "--";
}
}

const char* settingsRootLabel(UiLanguage language, unsigned row) {
    static const Translation values[] = {{"動作設定", "Operation"}, {"システム設定", "System settings"}, {"ストレージ操作", "Storage"},
        {"バージョン情報", "About"}, {"ライセンス情報", "Licenses"}, {"アラート", "Alerts"}};
    return at(language, values, row);
}
const char* settingsModeValue(UiLanguage language, uint8_t usb) {
    return usb==2 ? "Wi-Fi" : usb ? "USB" : language == UiLanguage::Japanese ? "GPSロガー" : "GPS logger";
}
const char* settingsSystemLabel(UiLanguage language, unsigned row) {
    static const Translation values[] = {{"モード", "Mode"}, {"GPS接続", "GPS source"},
        {"保存して再起動", "Save and restart"}, {"初期化", "Reset"}};
    return at(language, values, row);
}
const char* systemModeBlockText(const UiData& data, bool changed, UiLanguage language) {
    const bool ja = language == UiLanguage::Japanese;
    if (!loggerStopped(data.recording, data.storage.log)) return ja ? "先に記録を停止" : "Stop logger first";
    if (data.storage.busy || data.datasets().loading || data.datasets().applying) return ja ? "処理中です" : "Storage busy";
    if (data.storage.settings_state == SettingsState::Unavailable) return ja ? "設定を保存できません" : "Settings unavailable";
    if (!changed) return ja ? "変更はありません" : "No changes";
    return "";
}
const char* settingsOperationLabel(UiLanguage language, unsigned row) {
    static const Translation values[] = {{"表示言語", "Language"}, {"記録間隔", "Log interval"},
        {"SD保存間隔", "SD flush interval"}, {"消灯時間", "Screen timeout"}, {"明るさ", "Brightness"},
        {"データセット", "Dataset"}, {"音量", "Volume"}, {"省電力", "Power saving"}, {"GPS接続", "GPS source"}};
    return row < OperationRowCount ? at(language, values, row) : "--";
}
const char* settingsStorageLabel(UiLanguage language, unsigned row) {
    static const Translation values[] = {{"カード情報", "Card information"}, {"再マウント", "Remount"},
        {"安全な取り外し", "Safely remove"}, {"FAT32フォーマット", "Format as FAT32"}};
    return at(language, values, row);
}
const char* settingsLabel(UiLanguage language, SettingsLabel label) {
    static const Translation values[] = {{"FAT32初期化?", "Format as FAT32?"}, {"設定を初期化?", "Restore defaults?"},
        {"SD全消去 / 設定保持", "Erase SD; keep settings"}, {"Geoputer設定のみ", "Geoputer settings only"},
        {"SDデータは保持", "Keep SD data"}, {"取消", "Cancel"}, {"実行", "Confirm"},
        {"設定を初期値に戻す", "Restore defaults"}, {"カード容量", "Card size"}, {"総容量", "Total"},
        {"空き容量", "Free"}, {"非対応", "UNSUPPORTED"}, {"処理中です", "STORAGE WORKING..."},
        {"設定エラー", "SETTINGS ERROR"}};
    return at(language, values, static_cast<unsigned>(label));
}
const char* settingsCardState(UiLanguage language, CardState state) {
    static const Translation values[] = {{"確認中", "WAITING"}, {"カードなし", "NO CARD"},
        {"接続済", "MOUNTED"}, {"取外し可能", "SAFE TO REMOVE"}, {"FAT32が必要", "FAT32 REQUIRED"},
        {"SD異常", "SD ERROR"}};
    return at(language, values, static_cast<unsigned>(state));
}
const char* settingsMessage(UiLanguage language, const char* message) {
    if (language != UiLanguage::Japanese) return message;
    static const Translation values[] = {{"設定を保存しました", "SETTINGS SAVED"},
        {"設定エラー", "SETTINGS ERROR"}, {"SD操作に失敗", "SD OPERATION FAILED"},
        {"取り外し可能です", "SAFE TO REMOVE SD"}, {"FAT32初期化完了", "FAT32 FORMAT COMPLETE"},
        {"SD接続完了", "SD MOUNTED"}, {"処理中です", "STORAGE BUSY"},
        {"設定を変更できません", "SETTING NOT AVAILABLE"}, {"先に記録を停止", "STOP LOGGER FIRST"},
        {"SD操作が不正です", "INVALID SD OPERATION"}, {"設定を保存できません", "SETTINGS SAVE REJECTED"},
        {"SD操作を開始できません", "SD OPERATION REJECTED"}, {"設定を保存中", "SAVING SETTINGS..."},
        {"SD処理中", "SD WORKING..."}, {"ストレージ使用不可", "STORAGE UNAVAILABLE"},
        {"動作モード変更失敗", "MODE SWITCH FAILED"}, {"動作モード切替不可", "MODE SWITCH REJECTED"}};
    for (const auto& value : values) if (std::strcmp(message, value.english) == 0) return value.japanese;
    return message;
}
void settingsValueText(UiLanguage language, unsigned row, uint8_t choice, char* out, size_t size) {
    if (choice >= settingChoices(row)) { std::snprintf(out, size, "--"); return; }
    switch (static_cast<SettingId>(row)) {
        case SettingId::Language: std::snprintf(out, size, "%s", choice ? "English" : "日本語"); break;
        case SettingId::LogInterval: case SettingId::FlushInterval: case SettingId::ScreenTimeout: {
            const auto seconds = row == static_cast<unsigned>(SettingId::LogInterval) ? LogIntervals[choice] :
                row == static_cast<unsigned>(SettingId::FlushInterval) ? FlushIntervals[choice] : ScreenTimeouts[choice];
            if (row == static_cast<unsigned>(SettingId::ScreenTimeout) && !seconds)
                std::snprintf(out, size, "%s", language == UiLanguage::English ? "Never" : "消灯しない");
            else std::snprintf(out, size, language == UiLanguage::English ? "%u s" : "%u秒", seconds);
            break;
        }
        case SettingId::Brightness: std::snprintf(out, size, "%u%%", Brightnesses[choice]); break;
        case SettingId::Dataset:
            if(choice) std::snprintf(out, size, "%02u", choice);
            else std::snprintf(out, size, "%s", language == UiLanguage::English ? "None" : "未選択");
            break;
        case SettingId::Volume: std::snprintf(out,size,"%u",choice+1u);break;
        case SettingId::PowerSaving: {
            static constexpr const char* ja[]={"しない","通常","最大限"};
            static constexpr const char* en[]={"Off","Normal","Maximum"};
            std::snprintf(out,size,"%s",language==UiLanguage::Japanese?ja[choice]:en[choice]);break;
        }
        case SettingId::GpsMode: std::snprintf(out, size, "%s", choice ?
            (language == UiLanguage::English ? "External" : "外付け") :
            (language == UiLanguage::English ? "Mesh Kit" : "MESH")); break;
        case SettingId::Count: std::snprintf(out, size, "--"); break;
    }
}

}  // namespace geoputer

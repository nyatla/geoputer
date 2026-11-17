#include "LogPresentation.h"
#include "GpsUiRenderer.h"
#include "SdCardIcon.h"
#include "EscapeIcon.h"
#include "EnterIcon.h"
#include "ScreenNavigation.h"
#include "GpsControls.h"
#include "../../LogReplay.h"
#include "../../BuildInfo.h"
#include "LicenseText.h"
#include "SettingsLayout.h"
#include "SettingsText.h"
#include "AlarmText.h"
#include "AlarmBellIcon.h"
#include "GpsHeaderLayout.h"
#include "LivePlaceLayout.h"
#include "GpsPresentation.h"
#include "PlacePresentation.h"
#include "DatasetPresentation.h"
#include <cstdio>

namespace geoputer {
namespace {
constexpr int ViewHeaderTextY = SettingsLayout::HeaderTextY;
constexpr int ViewHeaderBottom = SettingsLayout::HeaderBottom;
}

TextRole GpsUiRenderer::placeRole(PlaceTextMode mode) {
    return mode==PlaceTextMode::Japanese16 ? TextRole::Place : TextRole::Ascii8x16;
}

bool GpsUiRenderer::begin(uint8_t color_depth, void* buffer, size_t capacity) {
    display_.setRotation(1);
    display_.setBrightness(102);
    return display_.width() == 240 && display_.height() == 135 &&
           frame_.begin(color_depth, buffer, capacity) && font_.begin();
}

void GpsUiRenderer::text(int x, int y, const char* value, uint16_t color) {
    font_.draw(frame_, x, y, value, TextRole::UI, color);
}

void GpsUiRenderer::valueText(int left, int right, int y, const char* value, uint16_t color) {
    // UI ASCII is 8 pixels wide. Never let oversized values cover neighbouring fields.
    const int width = static_cast<int>(std::strlen(value)) * 8;
    if (width > right - left) value = "OVR";
    text(right - static_cast<int>(std::strlen(value)) * 8, y, value, color);
}

void GpsUiRenderer::largeText(int x, int y, const char* value, uint16_t color) {
    font_.draw(frame_, x, y, value, TextRole::UiLarge, color);
}

void GpsUiRenderer::speedText(const char* value) {
    int x = LivePlaceLayout::SpeedRight - speedTextWidth(value);
    for (; *value; ++value) {
        const char character[] = {*value, '\0'};
        // Keep the native period bitmap and center its left-hand ink in
        // an eight-dot advance; the digit bitmaps remain sixteen dots wide.
        largeText(*value == '.' ? x + 2 : x, LivePlaceLayout::SpeedY, character);
        x += *value == '.' ? 8 : 16;
    }
}

static const char* recordingName(RecordingState state) {
    switch (state) {
        case RecordingState::Stopped: return "STOP";
        case RecordingState::Starting: return "WAIT";
        case RecordingState::Recording: return "REC";
        case RecordingState::Paused: return "PAUSE";
    }
    return "ERROR";
}

void GpsUiRenderer::drawDialog(const GpsUiModel& model,const UiSnapshot& content,UiLanguage language) {
    const bool japanese = language == UiLanguage::Japanese;
    const auto role = japanese ? TextRole::UiLarge : TextRole::Ascii8x16;
    const auto& dialog = model.dialog();
    const char* title = "";
    const char* detail = "";
    const char* accept = "";
    auto detail_role = role;
    char target_detail[40]{};
    if (dialog.kind() == DialogKind::GpsControl) {
        frame_.fillRect(8, 34, 224, 70, TFT_BLACK);
        frame_.drawFastHLine(8, 34, 224, TFT_WHITE);
        frame_.drawFastHLine(8, 103, 224, TFT_WHITE);
        const char* title = japanese ? "GPS・記録の操作" : "GPS / Logger control";
        font_.draw(frame_, (240-PixelFont::width(title, role))/2, 42, title, role, TFT_YELLOW);
        const auto state = model.data().recording.state;
        text(24, 64, state == RecordingState::Stopped ? "START LOG / GPS ACTIVE" :
            state == RecordingState::Paused ? "STOP / RESUME LOG" : "STOP / PAUSE LOG", TFT_LIGHTGREY);
        for (unsigned i = 0; i < dialog.choices(); ++i) {
            const char* label = gpsControlLabel(gpsControlAction(state,i), japanese);
            const int center = dialog.choices() == 1 ? 120 : 64 + i*112;
            const int x = center - PixelFont::width(label, role)/2;
            const auto color = !dialog.choiceEnabled(i) ? TFT_DARKGREY : dialog.choice() == i ? TFT_CYAN : TFT_WHITE;
            font_.draw(frame_, x, 81, label, role, color);
            if (dialog.choice() == i) frame_.drawFastHLine(x, 99, PixelFont::width(label, role), TFT_CYAN);
        }
        frame_.fillRect(0, 121, 240, 14, TFT_BLACK);

        return;
    }
    switch (dialog.kind()) {
        case DialogKind::BootFormat:
            title = japanese ? "SDをFAT32にする?" : "Format SD as FAT32?";
            detail = japanese ? "M5Launcherも消去" : "Erases all SD files";
            accept = japanese ? "初期化" : "Format";
            break;
        case DialogKind::BootDirectories:
            title = japanese ? "Geoputerを準備?" : "Set up Geoputer?";
            detail = japanese ? "基本フォルダを作成" : "Create directories";
            accept = japanese ? "作成" : "Create";
            break;
        case DialogKind::RestartLogger:
            title = japanese ? "通常起動へ戻る？" : "Restart logger?";
            accept = japanese ? "再起動" : "Restart";
            break;
        case DialogKind::DeleteLog: {
            title = !dialog.actionEnabled() ? (japanese ? "記録中は削除不可" : "Log is in use") :
                (japanese ? "このログを削除？" : "Delete this log?");
            accept = japanese ? "削除" : "Delete";
            const auto selected = content.selected_setting;
            if (selected < model.data().browse().count) detail = model.data().browse().names[selected];
            detail_role = TextRole::UI;
            break;
        }
        case DialogKind::FormatSd:
            title = settingsLabel(language, SettingsLabel::FormatQuestion);
            detail = settingsLabel(language, SettingsLabel::FormatScope);
            accept = settingsLabel(language, SettingsLabel::Confirm);
            break;
        case DialogKind::ResetSettings:
            title = settingsLabel(language, SettingsLabel::ResetQuestion);
            detail = settingsLabel(language, SettingsLabel::KeepSd);
            accept = settingsLabel(language, SettingsLabel::Confirm);
            break;
        case DialogKind::EnterWifiMode:
        case DialogKind::EnterUsbMode:
            title = japanese ? "保存して再起動？" : "Save and restart?";
            detail = dialog.actionEnabled() ? (dialog.kind()==DialogKind::EnterWifiMode?"Wi-Fi":"USB") : systemModeBlockText(model.data(), model.systemUsbMode(), language);
            accept = japanese ? "再起動" : "Restart";
            break;
        case DialogKind::ClearDataset:
            title=japanese?"選択を解除？":"Clear dataset?";
            detail=dialog.actionEnabled()?(japanese?"地名表示を停止":"Disable place lookup"):
                datasetErrorText(datasetRowBlock(model.data(),content.selected_setting),japanese);
            accept=japanese?"解除":"Clear";break;
        case DialogKind::RebuildIndex:case DialogKind::VerifyIndex:
            title=dialog.kind()==DialogKind::RebuildIndex?(japanese?"キャッシュ再生成？":"Rebuild cache?"):(japanese?"キャッシュを検証？":"Verify cache?");
            std::snprintf(target_detail,sizeof(target_detail),"DATASET %s / ALL LANGUAGES",model.datasetDialogId().c_str());
            if(dialog.actionEnabled()) {detail=target_detail;detail_role=TextRole::UI;}
            else detail=datasetErrorText(datasetRowBlock(model.data(),content.selected_setting),japanese);
            accept=japanese?"実行":"Run";break;
        case DialogKind::ExitUsbMode: case DialogKind::ExitWifiMode: case DialogKind::RegenerateWifi:
        case DialogKind::GpsControl: case DialogKind::None: return;
    }
    // Details are either a 16-dot scope message, or an ASCII filename wrapped
    // into 27-cell rows. Two rows cover the entire supported 47-byte filename.
    constexpr unsigned FilenameColumns = 27;
    const bool filename = detail_role == TextRole::UI;
    const bool second_line = filename && std::strlen(detail) > FilenameColumns;
    const int extra = filename ? (second_line ? 10 : 0) : (detail[0] ? 8 : 0);
    const int height = 65 + extra;
    const int top = (141 - height) / 2;
    const int choice_y = top + 42 + extra;
    frame_.fillRect(8, top, 224, height, TFT_BLACK);
    frame_.drawFastHLine(8, top, 224, TFT_WHITE);
    frame_.drawFastHLine(8, top + height - 1, 224, TFT_WHITE);
    font_.draw(frame_, (240 - PixelFont::width(title, role)) / 2, top + 6, title, role,
               dialog.actionEnabled() ? TFT_YELLOW : TFT_RED);
    if (filename) {
        char first[FilenameColumns + 1];
        std::snprintf(first, sizeof(first), "%.27s", detail);
        text((240 - PixelFont::width(first, TextRole::UI)) / 2, top + 26, first);
        if (second_line) {
            const auto* rest = detail + FilenameColumns;
            text((240 - PixelFont::width(rest, TextRole::UI)) / 2, top + 36, rest);
        }
    } else font_.draw(frame_, (240 - PixelFont::width(detail, role)) / 2, top + 26, detail, role, TFT_WHITE);
    const auto cancel_color = dialog.selected() ? TFT_WHITE : TFT_CYAN;
    const auto action_color = !dialog.actionEnabled() ? TFT_DARKGREY : dialog.selected() ? TFT_CYAN : TFT_WHITE;
    font_.draw(frame_, (dialog.selected() ? 152 : 56) - (japanese ? 24 : 16), choice_y, ">", role, TFT_CYAN);
    font_.draw(frame_, 56, choice_y, settingsLabel(language, SettingsLabel::Cancel), role, cancel_color);
    font_.draw(frame_, 152, choice_y, accept, role, action_color);
    frame_.fillRect(0, 121, 240, 14, TFT_BLACK);

}

void GpsUiRenderer::draw(const GpsUiModel& model) {
    const auto content = model.snapshot();
    const auto& data = model.data();
    if (model.mode() == DisplayMode::Background) { display_.setBrightness(0); return; }
    const auto settings = validSettings(data.settings) ? data.settings : defaultSettings();
    display_.setBrightness(Brightnesses[settings.brightness] * 255 / 100);
    const auto language = settings.language;
    frame_.setDimmed(model.confirming());
    frame_.fillSprite(TFT_BLACK);
    const bool live = content.screen == ScreenId::LivePlace || showingMap(content.screen,data);
    const bool view_header = live || content.screen == ScreenId::GpsLogger || isLogBrowser(content.screen) || content.screen == ScreenId::Settings;
    frame_.drawFastHLine(0, view_header ? ViewHeaderBottom : 22, 240, live ? TFT_WHITE : TFT_DARKGREY);
    frame_.drawFastHLine(0, live ? LivePlaceLayout::FooterTop : 118, 240, live ? TFT_WHITE : TFT_DARKGREY);


    if (content.screen == ScreenId::Boot) {
        drawBoot(model, settings);
    } else if(content.screen==ScreenId::Map) {
        drawLivePlace(model,content,settings);
        drawMap(model,content);
    } else if (content.screen == ScreenId::LivePlace) {
        drawLivePlace(model, content, settings);
    } else if(viewingLogPoint(content.screen) && data.browse().map_mode) {
        drawMap(model,content);
        if(!data.browse().loading && data.browse().error!=LogReadError::None)text(4,48,logReadErrorName(data.browse().error),TFT_RED);
    } else if(isLogBrowser(content.screen)) {
        drawLogBrowser(model, content, settings);
    } else if (content.screen == ScreenId::Menu) {
        text(4,7,"MENU",TFT_LIGHTGREY);
        const auto role = language == UiLanguage::Japanese ? TextRole::UiLarge : TextRole::Ascii8x16;
        for (unsigned row=0; row<MenuCount; ++row) {
            const auto color = content.selected_setting == row ? TFT_CYAN : TFT_WHITE;
            font_.draw(frame_,4,25+row*18,content.selected_setting==row?">":" ",role,color);
            font_.draw(frame_,20,25+row*18,menuLabel(row,language==UiLanguage::Japanese),role,color);
        }
    } else if (content.screen == ScreenId::GpsLogger) {
        drawGpsLogger(model);
    } else {
        drawSettings(model, content, settings);
    }
    present(model,content,language);
}

// GPS screens share status icons; dialogs retain priority over transient names.
void GpsUiRenderer::present(const GpsUiModel& model,const UiSnapshot& content,UiLanguage language) {
    frame_.setDimmed(model.confirming());
    if(hasPlaybackHeader(content.screen,model.data()))drawPlaybackHeader(model);
    else if(hasRuntimeHeader(content.screen))drawRuntimeHeader(model);
    frame_.setDimmed(false);
    if(model.screenNameVisible())drawScreenName(model);
    if(model.confirming())drawDialog(model,content,language);
    frame_.fillRect(0,121,240,14,TFT_BLACK);
    drawScreenGuide(model);
    drawHeaderStatus(model);
    frame_.pushSprite(0,0);
}
void GpsUiRenderer::drawRuntimeHeader(const GpsUiModel& model) {
    frame_.fillRect(0,0,gps_header::IconsLeft,16,TFT_BLACK);
    const auto& data=model.data();const auto state=data.recording.state;
    const bool error=mainError(data)!=nullptr;
    const char* status=error?"ERROR":data.storage.log.closing?"SAVING":recordingName(state);
    const auto color=error?TFT_RED:state==RecordingState::Recording?TFT_GREEN:state==RecordingState::Paused?TFT_YELLOW:TFT_LIGHTGREY;
    text(gps_header::StatusX,ViewHeaderTextY,status,color);
    char clock[16];model.clock().format(clock,sizeof(clock));text(gps_header::ClockX,ViewHeaderTextY,clock);
}
void GpsUiRenderer::drawPlaybackHeader(const GpsUiModel& model) {
    frame_.fillRect(0,0,gps_header::IconsLeft,16,TFT_BLACK);
    const auto& view=model.data().browse();
    text(gps_header::StatusX,ViewHeaderTextY,view.playing?"PLAY":"PAUSE",view.playing?TFT_GREEN:TFT_YELLOW);
    text(60,ViewHeaderTextY,view.replay_reverse?"<":">");
    text(76,ViewHeaderTextY,replay::speedLabel(view));
}
void GpsUiRenderer::drawScreenName(const GpsUiModel& model) {
    const char* label=model.screenNameText();const auto role=TextRole::UI;
    // Cover only the status/clock for one second; body, controls and icons stay visible.
    frame_.fillRect(0,0,gps_header::IconsLeft,16,TFT_BLACK);
    font_.draw(frame_,gps_header::StatusX,ViewHeaderTextY,label,role,TFT_CYAN);
}
void GpsUiRenderer::drawHeaderStatus(const GpsUiModel& model) {
    frame_.fillRect(gps_header::IconsLeft,0,240-gps_header::IconsLeft,16,TFT_BLACK);
    if(model.alarmSet())for(unsigned y=0;y<alarm_icon::Height;++y)for(unsigned x=0;x<alarm_icon::Width;++x)
        if(alarm_icon::Rows[y]&(1u<<(alarm_icon::Width-1-x)))frame_.drawPixel(gps_header::AlarmX+x,gps_header::AlarmY+y,model.alarmRinging()?TFT_YELLOW:TFT_WHITE);
    const bool active=model.sdAccessLit();
    for(int y=0;y<sd_icon::Height;++y)
        for(int x=0;x<sd_icon::Width;++x)
            if(sd_icon::Rows[y]&(1u<<(sd_icon::Width-1-x)))
                frame_.drawPixel(sd_icon::X+x,sd_icon::Y+y,
                    sd_icon::InkRows[y]&(1u<<(sd_icon::Width-1-x))?
                        (active?TFT_WHITE:TFT_DARKGREY):(active?TFT_BLUE:TFT_BLACK));
    char line[8];
    const auto battery = model.battery();
    const uint16_t battery_frame = battery.charge_known && battery.charging ? TFT_CYAN : TFT_WHITE;
    const uint16_t battery_fill = battery.percent >= 0 && battery.percent <= 10 ? TFT_RED :
        battery.percent >= 0 && battery.percent <= 20 ? TFT_YELLOW : TFT_GREEN;
    frame_.drawRect(180, 3, 18, 10, battery_frame);
    frame_.fillRect(198, 6, 2, 4, battery_frame);
    if (battery.percent >= 0) {
        const unsigned bars = battery.percent == 0 ? 0 : (battery.percent + 24) / 25;
        for (unsigned i = 0; i < bars; ++i) frame_.fillRect(182 + i * 4, 5, 3, 6, battery_fill);
        std::snprintf(line, sizeof(line), "%d%%", battery.percent);
    } else std::snprintf(line, sizeof(line), "--%%");
    valueText(204,236,4,line,battery.percent >= 0 && battery.percent <= 20 ? battery_fill : TFT_WHITE);
}

void GpsUiRenderer::drawKeyArrow(int x, int y, UiKey direction, bool enabled) {
    const auto color = enabled ? TFT_WHITE : TFT_DARKGREY;
    auto pixel = [&](int col,int row) {
        if(direction==UiKey::Down) row=6-row;
        else if(direction==UiKey::Left) std::swap(col,row);
        else if(direction==UiKey::Right) { std::swap(col,row);col=6-col; }
        frame_.drawPixel(x+col,y+row,color);
    };
    for(int i=0;i<7;++i)pixel(3,i);
    for(int i=1;i<4;++i){pixel(3-i,i);pixel(3+i,i);}
}

void GpsUiRenderer::drawScreenGuide(const GpsUiModel& model) {
    const auto row=model.shortcutGuide();
    int x=4;
    for(unsigned i=0;i<row.size();++i) {
        const auto hint=row[i];
        const auto color=hint.tone==ShortcutTone::Disabled?TFT_DARKGREY:
                         hint.tone==ShortcutTone::Accent?TFT_CYAN:TFT_WHITE;
        if(hint.key==UiKey::Escape) {
            for(unsigned y=0;y<escape_icon::Height;++y)for(unsigned col=0;col<escape_icon::Width;++col)
                if(escape_icon::Rows[y]&((1u<<(escape_icon::Width-1))>>col))frame_.drawPixel(x+col,122+y,color);
        } else if(hint.key==UiKey::Enter) {
            for(unsigned y=0;y<enter_icon::Height;++y)for(unsigned col=0;col<enter_icon::Width;++col)
                if(enter_icon::Rows[y]&((1u<<(enter_icon::Width-1))>>col))frame_.drawPixel(x+col,122+y,color);
        } else if(const auto character=shortcutCharacter(hint.key)) {
            const char text_value[]={character,0};text(x,124,text_value,color);
        } else drawKeyArrow(x,124,hint.key,hint.tone!=ShortcutTone::Disabled);
        x+=shortcutWidth(hint.key);
    }
}

}  // namespace geoputer

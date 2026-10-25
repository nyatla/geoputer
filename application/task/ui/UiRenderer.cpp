#include "LogPresentation.h"
#include "UiRenderer.h"
#include "ScreenNavigation.h"
#include "GpsControls.h"
#include "../../BuildInfo.h"
#include "LicenseText.h"
#include "SettingsLayout.h"
#include "SettingsText.h"
#include "LivePlaceLayout.h"
#include "PlacePresentation.h"
#include "DatasetPresentation.h"
#include <cstdio>

namespace geoputer {
namespace {
constexpr int ViewHeaderTextY = SettingsLayout::HeaderTextY;
constexpr int ViewHeaderBottom = SettingsLayout::HeaderBottom;
}

bool UiRenderer::begin(uint8_t color_depth) {
    display_.setRotation(1);
    display_.setBrightness(102);
    return display_.width() == 240 && display_.height() == 135 &&
           frame_.begin(color_depth) && font_.begin();
}

void UiRenderer::text(int x, int y, const char* value, uint16_t color) {
    font_.draw(frame_, x, y, value, TextRole::UI, color);
}

void UiRenderer::valueText(int left, int right, int y, const char* value, uint16_t color) {
    // UI ASCII is 8 pixels wide. Never let oversized values cover neighbouring fields.
    const int width = static_cast<int>(std::strlen(value)) * 8;
    if (width > right - left) value = "OVR";
    text(right - static_cast<int>(std::strlen(value)) * 8, y, value, color);
}

void UiRenderer::largeText(int x, int y, const char* value, uint16_t color) {
    font_.draw(frame_, x, y, value, TextRole::UiLarge, color);
}

void UiRenderer::speedText(const char* value) {
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

void UiRenderer::drawDialog(const UiModel& model, UiLanguage language) {
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
        drawDialogGuide(dialog);
        return;
    }
    switch (dialog.kind()) {
        case DialogKind::RestartLogger:
            title = japanese ? "通常起動へ戻る？" : "Restart logger?";
            accept = japanese ? "再起動" : "Restart";
            break;
        case DialogKind::PauseLog:
            title = japanese ? "記録を一時停止？" : "Pause logging?";
            accept = japanese ? "一時停止" : "Pause";
            break;
        case DialogKind::StopLog:
            title = japanese ? "記録を停止？" : "Stop logging?";
            accept = japanese ? "停止" : "Stop";
            break;
        case DialogKind::DeleteLog: {
            title = !dialog.actionEnabled() ? (japanese ? "記録中は削除不可" : "Log is in use") :
                (japanese ? "このログを削除？" : "Delete this log?");
            accept = japanese ? "削除" : "Delete";
            const auto selected = model.snapshot().selected_setting;
            if (selected < model.data().browse.count) detail = model.data().browse.names[selected];
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
        case DialogKind::EnterUsbMode:
            title = japanese ? "保存して再起動？" : "Save and restart?";
            detail = dialog.actionEnabled() ? "USB" : systemModeBlockText(model.data(), model.systemUsbMode(), language);
            accept = japanese ? "再起動" : "Restart";
            break;
        case DialogKind::ExitUsbMode:
            title = japanese ? "USBモードを終了？" : "Exit USB mode?";
            detail = japanese ? "PCの保存完了後に終了" : "Finish PC saves first";
            accept = japanese ? "終了" : "Exit";
            break;
        case DialogKind::ClearDataset:
            title=japanese?"選択を解除？":"Clear dataset?";
            detail=dialog.actionEnabled()?(japanese?"地名表示を停止":"Disable place lookup"):
                datasetErrorText(datasetRowBlock(model.data(),model.snapshot().selected_setting),japanese);
            accept=japanese?"解除":"Clear";break;
        case DialogKind::RebuildIndex:case DialogKind::VerifyIndex:
            title=dialog.kind()==DialogKind::RebuildIndex?(japanese?"キャッシュ再生成？":"Rebuild cache?"):(japanese?"キャッシュを検証？":"Verify cache?");
            std::snprintf(target_detail,sizeof(target_detail),"DATASET %02u / ALL LANGUAGES",model.datasetDialogNumber());
            if(dialog.actionEnabled()) {detail=target_detail;detail_role=TextRole::UI;}
            else detail=datasetErrorText(datasetRowBlock(model.data(),model.snapshot().selected_setting),japanese);
            accept=japanese?"実行":"Run";break;
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
    drawDialogGuide(dialog);
}

void UiRenderer::draw(const UiModel& model) {
    const auto content = model.snapshot();
    const auto& data = model.data();
    if (model.mode() == DisplayMode::Background) { display_.setBrightness(0); return; }
    const auto settings = validSettings(data.settings) ? data.settings : defaultSettings();
    display_.setBrightness(Brightnesses[settings.brightness] * 255 / 100);
    const auto language = settings.language;
    frame_.setDimmed(model.confirming());
    frame_.fillSprite(TFT_BLACK);
    const bool live = content.screen == ScreenId::LivePlace || content.screen == ScreenId::Map;
    const bool view_header = live || content.screen == ScreenId::GpsLogger || isLogBrowser(content.screen) || content.screen == ScreenId::Settings;
    frame_.drawFastHLine(0, view_header ? ViewHeaderBottom : 22, 240, live ? TFT_WHITE : TFT_DARKGREY);
    frame_.drawFastHLine(0, live ? LivePlaceLayout::FooterTop : 118, 240, live ? TFT_WHITE : TFT_DARKGREY);
    if (content.screen == ScreenId::UsbStorage) {
        drawUsb(model);
        frame_.setDimmed(false);
        if (model.confirming()) drawDialog(model,language);
        frame_.pushSprite(0,0); return;
    }
    if (content.screen == ScreenId::Boot) {
        drawBoot(model, content, settings);
    } else if(content.screen==ScreenId::Map) {
        drawLivePlace(model,content,settings);
        drawMap(model);
    } else if (content.screen == ScreenId::LivePlace) {
        drawLivePlace(model, content, settings);
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
        drawGpsLogger(model, content, settings);
    } else {
        drawSettings(model, content, settings);
    }
    if (content.screen == ScreenId::Settings ||
        (!data.boot.maintenance && (isScreenRoot(content.screen, content.page) || isLogBrowser(content.screen)))) {
        frame_.fillRect(0, live ? 121 : 119, 240, live ? 14 : 16, TFT_BLACK);
        drawScreenGuide(model);
    }
    if (content.action.kind != ActionKind::None) {
        frame_.fillRect(0, 119, 120, 16, TFT_BLACK);
        text(4, 124, "WAIT...", TFT_CYAN);
    }
    frame_.setDimmed(false);
    if (model.confirming()) drawDialog(model, language);
    frame_.pushSprite(0, 0);
}

void UiRenderer::drawBoot(const UiModel& model, const UiSnapshot&, const Settings& settings) {
    const auto& data = model.data();
    char line[64];
    text(4, 8, "GEOPUTER", TFT_CYAN);
    text(164, 8, "STARTUP", TFT_LIGHTGREY);
    for (unsigned i = 0; i < BootStatus::Count; ++i) {
        const auto& check = data.boot.checks[i];
        const bool passed = check.state == BootCheckState::Passed;
        const bool failed = check.state == BootCheckState::Failed;
        const bool warning = check.state == BootCheckState::Warning;
        const bool checking = check.state == BootCheckState::Checking;
        const auto color = passed ? TFT_GREEN : failed ? TFT_RED : warning ? TFT_YELLOW : checking ? TFT_CYAN : TFT_DARKGREY;
        const int y = 29 + i * 14;
        text(4, y, passed ? "[+]" : failed ? "[X]" : warning ? "[!]" : checking ? "[~]" : "[.]", color);
        text(36, y, bootItemName(i), color);
        text(156, y, check.detail[0] ? check.detail : "WAIT", color);
    }
    if(data.storage.dataset_failure.number) formatDatasetRelease(data.storage.dataset_failure,line,sizeof(line));
    else if(settings.dataset)std::snprintf(line,sizeof(line),"GPS:MESH  DATASET:%02u",settings.dataset);
    else std::snprintf(line,sizeof(line),"GPS:MESH  DATASET:NONE");
    text(4,110,line,TFT_LIGHTGREY);
    text(4, 124, "ESC MAINT", TFT_CYAN);
    text(132, 124, data.boot.maintenance ? "ENTERING..." : data.boot.complete ? "ENTER START" : "CHECKING...",
         data.boot.complete ? TFT_WHITE : TFT_CYAN);
}

void UiRenderer::drawLivePlace(const UiModel& model, const UiSnapshot& content, const Settings& settings) {
    const auto& data = model.data();
    const auto language = settings.language;
    const bool japanese = language == UiLanguage::Japanese;
    char line[64];
    const auto& gps = data.gps;
    const char* error = mainError(data);
    char dataset_notice[40]{};
    if(model.datasetFailureVisible() && !data.storage.dataset_failure.save_failed) {
        formatDatasetRelease(data.storage.dataset_failure,dataset_notice,sizeof(dataset_notice));error=dataset_notice;
    }
    const bool storage_error = error != nullptr;
    const auto rec = data.recording.state;
    text(4, ViewHeaderTextY, storage_error ? "ERROR" : recordingName(rec), storage_error ? TFT_RED :
         rec == RecordingState::Recording ? TFT_GREEN : rec == RecordingState::Paused ? TFT_YELLOW : TFT_LIGHTGREY);
    model.clock().format(line, sizeof(line)); text(52, 4, line);
    const auto rg = data.storage.rg.state;
    const uint16_t rg_color = rg == RgCacheState::Ready ? TFT_GREEN : rg == RgCacheState::Rebuilding ? TFT_YELLOW : TFT_DARKGREY;
    if (rg != RgCacheState::Unprepared) frame_.fillRect(124, 2, 20, 12, rg_color);
    else frame_.drawRect(124, 2, 20, 12, rg_color);
    text(126, 4, "RG", rg == RgCacheState::Unprepared ? TFT_DARKGREY : TFT_BLACK);
    if (model.sdAccessLit()) frame_.fillRect(152, 2, 20, 12, TFT_BLUE);
    else frame_.drawRect(152, 2, 20, 12, TFT_DARKGREY);
    text(154, 4, "SD", model.sdAccessLit() ? TFT_WHITE : TFT_DARKGREY);
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
    if(content.screen==ScreenId::Map)return;
    const auto issue = placeIssue(data);
    if (datasetInactive(issue)) {
        const auto message = datasetIssueText(issue, japanese);
        for (unsigned i = 0; i < 2; ++i)
            font_.draw(frame_, LivePlaceLayout::AddressX, LivePlaceLayout::AddressY + i * LivePlaceLayout::AddressStep,
                       i ? message.second : message.first, TextRole::Place, TFT_WHITE);
    } else if (hasPlace(data) || data.place_state == PlaceState::Ready) {
        for (unsigned i = 0; i < 2; ++i)
            font_.draw(frame_, LivePlaceLayout::AddressX, LivePlaceLayout::AddressY + i * LivePlaceLayout::AddressStep,
                       data.place[i], TextRole::Place, TFT_WHITE);
    } else if (data.place_state == PlaceState::NoFix) largeText(4, 30, "WAIT GPS FIX", TFT_WHITE);
    else text(4, 30, placeStateName(data.place_state), TFT_LIGHTGREY);
    frame_.drawFastHLine(0, LivePlaceLayout::AddressBottom, 240, TFT_WHITE);
    frame_.fillRect(LivePlaceLayout::DividerX, LivePlaceLayout::AddressBottom, 1,
                    LivePlaceLayout::InfoBottom - LivePlaceLayout::AddressBottom + 1, TFT_WHITE);
    frame_.drawFastHLine(0, LivePlaceLayout::SpeedBottom, LivePlaceLayout::DividerX, TFT_WHITE);
    frame_.drawFastHLine(0, LivePlaceLayout::InfoBottom, 240, TFT_WHITE);
    const auto detail = formatLiveGps(gps, datasetInactive(issue) ? GpsFix{} : data.place_fix);
    text(4, 62, "SPD");
    speedText(detail.speed);
    text(LivePlaceLayout::SpeedUnitX, LivePlaceLayout::SpeedUnitY, detail.speed_unit);
    text(4, 85, "HDG");
    char heading[16]="--";
    if (gps.fix.valid && gps.reception==GpsReception::Fix && (gps.fix.fields & GpsCourse) &&
        std::isfinite(gps.fix.course_deg) && gps.fix.course_deg>=0 && gps.fix.course_deg<360)
        std::snprintf(heading,sizeof(heading),"%ld",std::lround(gps.fix.course_deg)%360);
    largeText(84-static_cast<int>(std::strlen(heading))*16,89,heading);
    largeText(84,89,"°");
    static const char* const labels[] = {"LAT", "LON", "ALT", "SAT"};
    const char* const values[] = {detail.latitude, detail.longitude, detail.altitude, detail.satellites};
    for (unsigned row = 0; row < 4; ++row) {
        const int y = LivePlaceLayout::DetailY + row * LivePlaceLayout::DetailStep;
        text(LivePlaceLayout::DetailLabelX, y, labels[row]);
        valueText(LivePlaceLayout::DetailValueX,236,y,values[row]);
    }
    if (error) {
        frame_.fillRect(0, LivePlaceLayout::ErrorY, 240, LivePlaceLayout::ErrorHeight, TFT_RED);
        if (data.storage.log.error != LogError::None) {
            std::snprintf(line, sizeof(line), "%s PTS%lu", error, static_cast<unsigned long>(data.storage.log.accepted));
            text(4, LivePlaceLayout::LogY, std::strlen(line) <= 29 ? line : error);
        } else text(4, LivePlaceLayout::LogY, error);
    } else {
        const auto seconds = data.recording.elapsed_ms / 1000;
        std::snprintf(line, sizeof(line), "%02lu:%02lu:%02lu", static_cast<unsigned long>(seconds / 3600),
                      static_cast<unsigned long>(seconds / 60 % 60), static_cast<unsigned long>(seconds % 60));
        text(4,LivePlaceLayout::LogY,"LOG");valueText(28,116,LivePlaceLayout::LogY,line);
        text(124,LivePlaceLayout::LogY,"PTS");
        std::snprintf(line,sizeof(line),"%lu",static_cast<unsigned long>(data.storage.log.accepted));
        valueText(156,236,LivePlaceLayout::LogY,line);
    }
}

void UiRenderer::drawLogBrowser(const UiModel& model, const UiSnapshot& content, const Settings& settings) {
    const auto& data = model.data();
    const auto language = settings.language;
    const bool japanese = language == UiLanguage::Japanese;
    const auto item_role = japanese ? TextRole::UiLarge : TextRole::Ascii8x16;
    auto item = [&](int x, int y, const char* value, uint16_t color = TFT_WHITE) {
        font_.draw(frame_, x, y, value, item_role, color);
    };
    char line[64];
    const auto& view=data.browse;
    const bool detail=content.screen==ScreenId::LogDetail;
    const bool points=content.screen==ScreenId::LogPoint;
    text(4,ViewHeaderTextY,points?"LOG POINT":detail?"LOG DETAIL":"LOGS",TFT_LIGHTGREY);
    if(!points) {
        text(220,ViewHeaderTextY,"SD",model.sdAccessLit()?TFT_BLUE:TFT_DARKGREY);
        if(detail && !view.loading && view.summary.session_id && view.summary.session_id==data.storage.log.session_id && data.storage.log.open)
            text(176,ViewHeaderTextY,data.recording.state==RecordingState::Paused?"PAUSE":"REC",TFT_GREEN);
    }
    if(!detail && !points) {
        for(unsigned n=0;n<view.count && n<LogListRows;++n) {
            const int y=27+n*12;
            const auto color=view.loading?TFT_DARKGREY:n==content.selected_setting?TFT_CYAN:TFT_WHITE;
            text(4,y,n==content.selected_setting?">":" ",color);
            char name[27]; std::snprintf(name,sizeof(name),"%.26s",view.names[n]);
            if(std::strlen(view.names[n])>26) std::memcpy(name+23,"...",4);
            text(20,y,name,color);
            if(view.active_mask&(1u<<n)) text(228,y,"R",TFT_GREEN);
        }
        if(!view.count && !view.loading && view.error==LogReadError::None) text(4,47,"NO LOG FILES",TFT_LIGHTGREY);
        std::snprintf(line,sizeof(line),"PAGE %lu%s%s",static_cast<unsigned long>(view.page+1),view.has_newer?"  ^":"",view.has_older?"  v":"");
        if(view.count && !view.loading && view.error==LogReadError::None) text(4,106,line,TFT_LIGHTGREY);
    } else if(detail) {
        char name[30]; std::snprintf(name,sizeof(name),"%.29s",view.name); text(4,27,name);
        if(std::strlen(view.name)>29) text(4,37,view.name+29);
        char date[24];
        formatLogDateTime(view.summary.first_utc,date,sizeof(date)); std::snprintf(line,sizeof(line),"FIRST %s",date); text(4,49,line);
        formatLogDateTime(view.summary.last_utc,date,sizeof(date)); std::snprintf(line,sizeof(line),"LAST  %s",date); text(4,59,line);
        text(4,69,"PTS");std::snprintf(line,sizeof(line),"%lu",static_cast<unsigned long>(view.summary.records));valueText(28,116,69,line);
        std::snprintf(line,sizeof(line),"%llu",static_cast<unsigned long long>(view.summary.bytes));valueText(124,228,69,line);text(228,69,"B");
        if(view.summary.tail && !view.loading) text(128,ViewHeaderTextY,"TAIL",TFT_YELLOW);
        for(unsigned n=0;n<2;++n) {
            const auto color=view.loading || (!n && (!view.summary.session_id || !view.summary.records)) ? TFT_DARKGREY : content.selected_setting==n?TFT_CYAN:TFT_WHITE;
            const int y=n?101:83;
            item(4,y,content.selected_setting==n?">":" ",color);
            item(20,y,n?(japanese?"再読込":"RELOAD"):(japanese?"測位点を見る":"VIEW POINTS"),color);
        }
    } else {
        std::snprintf(line,sizeof(line),"%lu/%lu",static_cast<unsigned long>(view.index+1),static_cast<unsigned long>(view.summary.records));
        text(236-static_cast<int>(std::strlen(line))*8,ViewHeaderTextY,line);
        if(view.point.valid) {
            char date[24]; formatLogDateTime(view.point.fields&GpsTime?view.point.utc_epoch_ms:UINT64_MAX,date,sizeof(date));
            std::snprintf(line,sizeof(line),"%s%s",date,view.point.fields&GpsTime?" JST":""); text(4,27,line);
            text(4,43,"LAT");std::snprintf(line,sizeof(line),"%+.6f",view.point.latitude);valueText(28,116,43,line);
            text(124,43,"LON");std::snprintf(line,sizeof(line),"%+.6f",view.point.longitude);valueText(148,236,43,line);
            if(view.point.fields&GpsSpeed) std::snprintf(line,sizeof(line),"%.1f",view.point.speed_kmh); else std::snprintf(line,sizeof(line),"--");
            text(4,65,"SPD");valueText(36,140,65,line);text(148,65,"km/h");
            if(view.point.fields&GpsCourse) std::snprintf(line,sizeof(line),"%.2f",view.point.course_deg); else std::snprintf(line,sizeof(line),"--");
            text(4,75,"HDG");valueText(36,140,75,line);text(148,75,"deg");
            if(view.point.fields&GpsAltitude) std::snprintf(line,sizeof(line),"%.3f",view.point.altitude_m); else std::snprintf(line,sizeof(line),"--");
            text(4,85,"ALT");valueText(36,140,85,line);text(148,85,"m");
            if(view.point.fields&GpsSatellites) std::snprintf(line,sizeof(line),"%u",view.point.satellites); else std::snprintf(line,sizeof(line),"--");
            text(4,97,"SAT");valueText(28,84,97,line);
            if(view.point.fields&GpsHdop) std::snprintf(line,sizeof(line),"%.2f",view.point.hdop); else std::snprintf(line,sizeof(line),"--");
            text(100,97,"HDOP");valueText(132,236,97,line);
        }
    }
    if(view.loading) text(detail?132:4,points?109:detail?ViewHeaderTextY:106,view.deleting?"DELETING...":"READING...",TFT_YELLOW);
    else if(view.error!=LogReadError::None && !model.confirming()) {
        if(detail) { if(view.summary.session_id && view.error==LogReadError::Corrupt) text(128,ViewHeaderTextY,"CRC!",TFT_RED); else { frame_.fillRect(0,49,240,8,TFT_BLACK); text(4,49,logReadErrorName(view.error),TFT_RED); } }
        else text(4,points?109:106,logReadErrorName(view.error),TFT_RED);
    }
}

void UiRenderer::drawGpsLogger(const UiModel& model, const UiSnapshot&, const Settings& settings) {
    const auto& data = model.data();
    const auto& gps = data.gps;
    const auto& fix = gps.fix;
    const auto& log = data.storage.log;
    char line[80];
    text(4,ViewHeaderTextY,"GPS / LOGGER",TFT_LIGHTGREY);
    text(148,ViewHeaderTextY,log.closing ? "SAVING" : recordingName(data.recording.state),TFT_CYAN);
    const auto detail = formatLiveGps(gps, {});
    // Two 116-pixel columns. Coordinates share a row; labels never depend on values.
    text(4,23,"LAT"); valueText(28,116,23,detail.latitude);
    text(124,23,"LON"); valueText(148,236,23,detail.longitude);
    text(4,33,"GPS"); text(36,33,gpsReceptionName(gps.reception));
    text(124,33,"RX");
    if (gps.received) std::snprintf(line,sizeof(line),"%lu",static_cast<unsigned long>((data.recording.now_ms-gps.received_at_ms)/1000));
    else std::snprintf(line,sizeof(line),"--");
    valueText(148,228,33,line);text(228,33,"s");
    char hdop[16]="--", satellites[8]="--";
    if(fix.fields & GpsSatellites)std::snprintf(satellites,sizeof(satellites),"%u",fix.satellites);
    if((fix.fields & GpsHdop) && std::isfinite(fix.hdop))std::snprintf(hdop,sizeof(hdop),"%.1f",fix.hdop);
    char altitude[24]="--";
    if(fix.valid && gps.reception==GpsReception::Fix && (fix.fields & GpsAltitude) && std::isfinite(fix.altitude_m))
        std::snprintf(altitude,sizeof(altitude),"%.1f",fix.altitude_m);
    text(4,43,"ALT");valueText(28,108,43,altitude);text(108,43,"m");
    text(124,43,"HDOP");valueText(156,236,43,hdop);
    char heading[16]="--";
    if(fix.valid && gps.reception==GpsReception::Fix && (fix.fields & GpsCourse) && std::isfinite(fix.course_deg) && fix.course_deg>=0 && fix.course_deg<360)
        std::snprintf(heading,sizeof(heading),"%ld",std::lround(fix.course_deg)%360);
    text(4,53,"SPD");valueText(28,84,53,detail.speed);text(84,53,"km/h");
    text(124,53,"HDG");valueText(148,212,53,heading);text(212,53,"deg");
    text(4,63,"SAT");valueText(28,116,63,satellites);
    text(124,63,gps.reception==GpsReception::Unavailable?"GPS ERROR":"GPS ACTIVE",TFT_LIGHTGREY);
    const auto seconds = data.recording.elapsed_ms/1000;
    std::snprintf(line,sizeof(line),"%02lu:%02lu:%02lu",static_cast<unsigned long>(seconds/3600),
        static_cast<unsigned long>(seconds/60%60),static_cast<unsigned long>(seconds%60));
    text(4,73,"LOG");valueText(28,108,73,line);
    text(124,73,"BUF");std::snprintf(line,sizeof(line),"%u",log.buffered);valueText(148,212,73,line);text(212,73,"/64");
    text(4,83,"SAVED");std::snprintf(line,sizeof(line),"%lu",static_cast<unsigned long>(log.saved));valueText(44,116,83,line);
    text(124,83,"LOST");std::snprintf(line,sizeof(line),"%lu",static_cast<unsigned long>(data.recording.dropped));valueText(156,236,83,line);
    if(log.error != LogError::None) text(4,93,logErrorName(log.error),TFT_RED);
    else if(data.message[0]) text(4,93,data.message,TFT_YELLOW);
    else {
        std::snprintf(line,sizeof(line),"%.29s",log.name[0]?log.name:"FILE --");text(4,93,line,TFT_LIGHTGREY);
        if(std::strlen(log.name)>29)text(4,103,log.name+29,TFT_LIGHTGREY);
    }
    (void)settings;
}

void UiRenderer::drawSettings(const UiModel& model, const UiSnapshot& content, const Settings& settings) {
    const auto& data = model.data();
    const auto language = settings.language;
    const bool japanese = language == UiLanguage::Japanese;
    const auto item_role = japanese ? TextRole::UiLarge : TextRole::Ascii8x16;
    auto item = [&](int x, int y, const char* value, uint16_t color = TFT_WHITE) {
        font_.draw(frame_, x, y, value, item_role, color);
    };
    char line[64];
    auto notice = [&](const char* value, uint16_t color = TFT_LIGHTGREY) {
        frame_.fillRect(0,106,240,12,TFT_BLACK);
        text(4,108,value,color);
    };
    const char* title = model.cardInfo() ? "CARD INFO" :
                        content.page == SettingsPage::Root ? (data.boot.maintenance ? "MAINTENANCE" : "SETTINGS") :
                        content.page == SettingsPage::Operation ? "OPERATION" :
                        content.page == SettingsPage::System ? "SYSTEM" :
                        content.page == SettingsPage::Storage ? "STORAGE" :
                        content.page == SettingsPage::Reset ? "RESET" :
                        content.page == SettingsPage::About ? "ABOUT" : "LICENSES";
    auto label = [&](SettingsLabel id) { return settingsLabel(language, id); };
    text(4, ViewHeaderTextY, title, TFT_LIGHTGREY);
    const char* gps_state = data.gps.reception == GpsReception::Waiting ? "WAIT" : gpsReceptionName(data.gps.reception);
    std::snprintf(line, sizeof(line), "GPS:%s", gps_state);
    if (content.page == SettingsPage::Licenses) {
        std::snprintf(line,sizeof(line),"%lu/%lu",static_cast<unsigned long>(model.licenseOffset()+1),static_cast<unsigned long>(licenseLineCount()));
        valueText(92,236,ViewHeaderTextY,line,TFT_LIGHTGREY);
    } else if (content.page != SettingsPage::About)
        valueText(132,236,ViewHeaderTextY,line,data.gps.fix.valid ? TFT_GREEN : TFT_LIGHTGREY);
    if (content.page == SettingsPage::DatasetList || content.page == SettingsPage::DatasetDetail) {
        const auto& view = data.datasets;
        frame_.fillRect(0,0,240,ViewHeaderBottom,TFT_BLACK);text(4,ViewHeaderTextY,content.page==SettingsPage::DatasetDetail?"DATASET DETAIL":
            view.purpose==DatasetPurpose::Rebuild?"REBUILD CACHE":view.purpose==DatasetPurpose::Verify?"VERIFY CACHE":"DATASETS",TFT_WHITE);
        if (view.loading || view.applying) {
            item(4,27,view.applying?(view.progress.verifying?(japanese?"検証中です":"Verifying..."):(japanese?"適用中です":"Applying...")):(japanese?"読込中です":"Loading..."),TFT_YELLOW);
            if(view.applying) {
                item(4,51,indexPhaseText(view.progress.phase,japanese),TFT_WHITE);
                frame_.drawRect(4,76,232,12,TFT_LIGHTGREY);frame_.fillRect(6,78,228*view.progress.percent/100,8,TFT_GREEN);
                std::snprintf(line,sizeof(line),"%s %u%%  %lus",view.progress.language,view.progress.percent,static_cast<unsigned long>(view.progress.elapsed_ms/1000));text(4,96,line);
            }
        } else if (view.error != DatasetError::None) {
            char message_line[80];
            for (unsigned row=0;row<3;++row) {
                datasetTextLines(datasetErrorText(view.error,japanese),japanese,row,message_line,sizeof(message_line));
                item(4,27+row*20,message_line,TFT_RED);
            }
            if (view.error == DatasetError::TooManyChunks) {
                std::snprintf(line,sizeof(line),"%llu / %u",static_cast<unsigned long long>(view.info.chunks),static_cast<unsigned>(config::MaxDatasetChunks));
                text(4,91,line,TFT_YELLOW);
            }
        } else if (content.page == SettingsPage::DatasetList) {
            if (!view.count) item(4,27,japanese?"候補がありません":"No datasets",TFT_YELLOW);
            for (unsigned row=0;row<view.count;++row) {
                const int y=27+row*20;
                const auto blocked=datasetRowBlock(data,row);
                const auto color=blocked!=DatasetError::None?TFT_DARKGREY:row==content.selected_setting?TFT_CYAN:TFT_WHITE;
                item(4,y,row==content.selected_setting?">":" ",color);
                if(!view.rows[row].number) item(20,y,japanese?"未選択":"None",color);
                else {
                    std::snprintf(line,sizeof(line),"%02u",view.rows[row].number);
                    font_.draw(frame_,20,y,line,TextRole::Ascii8x16,color);
                    datasetSingleLine(view.rows[row].title,japanese,line,sizeof(line),24);item(44,y,line,color);
                }
            }
            const auto reason=view.count?datasetRowBlock(data,content.selected_setting):DatasetError::None;
            if(reason!=DatasetError::None) notice(datasetErrorText(reason,japanese));
        } else {
            const auto count=datasetDetailRows(view,japanese);
            const unsigned first=SettingsLayout::firstVisible(content.selected_setting,count);
            for (unsigned n=0;n<4 && first+n<count;++n) {
                const unsigned row=first+n;const int y=27+n*20;
                const auto color=row==count-1 && datasetSelectionBlock(data,true)!=DatasetError::None?TFT_DARKGREY:row==content.selected_setting?TFT_CYAN:TFT_WHITE;
                item(4,y,row==content.selected_setting?">":" ",color);
                const bool ascii=datasetDetailLine(view,row,japanese,line,sizeof(line));
                font_.draw(frame_,20,y,line,ascii?TextRole::Ascii8x16:item_role,color);
            }
            std::snprintf(line,sizeof(line),"%u/%u",content.selected_setting+1u,count);text(4,110,line);
            const auto reason=datasetSelectionBlock(data,true);
            if(content.selected_setting==count-1 && reason!=DatasetError::None) {
                notice(datasetErrorText(reason,japanese));
            }
        }
    } else if (model.cardInfo()) {
        if (data.storage.card_state == CardState::Mounted)
            std::snprintf(line, sizeof(line), "FAT%u %s", data.storage.fat_type, settingsCardState(language, data.storage.card_state));
        else if (data.storage.fat_type)
            std::snprintf(line, sizeof(line), "FAT%u %s", data.storage.fat_type, label(SettingsLabel::Unsupported));
        else std::snprintf(line, sizeof(line), "%s", settingsCardState(language, data.storage.card_state));
        item(4, 27, line, data.storage.card_state == CardState::Mounted ? TFT_GREEN : TFT_YELLOW);
        constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
        std::snprintf(line, sizeof(line), "%s %.1fG", label(SettingsLabel::CardSize), data.storage.card_bytes / GiB); item(4, 47, line);
        std::snprintf(line, sizeof(line), "%s %.1fG", label(SettingsLabel::Total), data.storage.total_bytes / GiB); item(4, 67, line);
        std::snprintf(line, sizeof(line), "%s %.1fG", label(SettingsLabel::Free), data.storage.free_bytes / GiB); item(4, 87, line);
    } else if (content.page == SettingsPage::Root) {
        const unsigned first = SettingsLayout::firstVisible(content.selected_setting, SettingsRootCount);
        for (unsigned visible = 0; visible < SettingsLayout::VisibleRows && first + visible < SettingsRootCount; ++visible) {
            const unsigned row = first + visible;
            const int y = SettingsLayout::FirstRowY + visible * SettingsLayout::RowStep;
            const auto color = row == content.selected_setting ? TFT_CYAN : TFT_WHITE;
            item(4, y, row == content.selected_setting ? ">" : " ", TFT_CYAN);
            item(20, y, settingsRootLabel(language, row), color);

        }
        std::snprintf(line, sizeof(line), "%u/%u", content.selected_setting + 1u, SettingsRootCount);
        text(4, LivePlaceLayout::LogY, line);
    } else if (content.page == SettingsPage::System) {
        const bool usb = model.editing() ? model.editValue() != 0 : model.systemUsbMode();
        const char* reason = systemModeBlockText(data, model.systemUsbMode(), language);
        for (unsigned row = 0; row < SettingsSystemCount; ++row) {
            const int y = SettingsLayout::FirstRowY + row * SettingsLayout::RowStep;
            const bool selected = row == content.selected_setting;
            const auto color = row == 1 || (row == 2 && reason[0]) || (row == SettingsSystemResetRow && data.storage.busy) ? TFT_DARKGREY : selected ? TFT_CYAN : TFT_WHITE;
            item(4,y,selected ? ">" : " ",color);
            item(SettingsLayout::LabelX,y,settingsSystemLabel(language,row),color);
            if (row < 2) {
                const char* value = row == 0 ? settingsModeValue(language,usb) : "MESH";
                font_.draw(frame_,SettingsLayout::ValueRight-PixelFont::width(value,item_role),y,value,item_role,color);
            }
        }
        const char* help = content.selected_setting == 1 ? (japanese ? "外付けGPSは未対応" : "External GPS unsupported") :
            content.selected_setting == 2 ? reason : "";
        if (help[0]) {
            notice(help);
        }
    } else if (content.page == SettingsPage::Operation) {
        const unsigned first = SettingsLayout::firstVisible(content.selected_setting, OperationRowCount);
        for (unsigned visible = 0; visible < SettingsLayout::VisibleRows && first + visible < OperationRowCount; ++visible) {
            const unsigned row = first + visible;
            const int y = SettingsLayout::FirstRowY + visible * SettingsLayout::RowStep;
            const bool selected = row == content.selected_setting;
            const uint16_t color = !settingEditable(row) && row != static_cast<unsigned>(SettingId::Dataset) ? TFT_DARKGREY : selected ? TFT_CYAN : TFT_WHITE;
            item(4, y, selected ? ">" : " ", TFT_CYAN);
            item(SettingsLayout::LabelX, y, settingsOperationLabel(language, row), color);
            char value[24];
            settingsValueText(language, row, selected && model.editing() ? model.editValue() : settingValue(settings, row), value, sizeof(value));
            const auto value_role = row == static_cast<unsigned>(SettingId::Language) ? TextRole::Ascii8x16 : item_role;
            font_.draw(frame_, SettingsLayout::ValueRight - PixelFont::width(value, value_role), y, value, value_role, color);
        }
        std::snprintf(line, sizeof(line), "%u/%u", content.selected_setting + 1u, OperationRowCount);
        text(4, LivePlaceLayout::LogY, line, TFT_WHITE);
    } else if(content.page==SettingsPage::DatasetMenu) {
        frame_.fillRect(0,0,240,ViewHeaderBottom,TFT_BLACK);text(4,ViewHeaderTextY,"DATASET");
        if(settings.dataset) std::snprintf(line,sizeof(line),"CURRENT %02u / ALL LANGUAGES",settings.dataset);
        else std::snprintf(line,sizeof(line),"CURRENT NONE");
        text(4,26,line,TFT_LIGHTGREY);
        for(unsigned row=0;row<3;++row) {
            const auto blocked=datasetMenuBlock(data,row);
            const auto color=blocked!=DatasetError::None?TFT_DARKGREY:row==content.selected_setting?TFT_CYAN:TFT_WHITE;
            const int y=43+row*20;item(4,y,row==content.selected_setting?">":" ",color);
            item(20,y,datasetMenuLabel(row,japanese),color);
        }
        if(data.storage.dataset_failure.number) {
            formatDatasetRelease(data.storage.dataset_failure,line,sizeof(line));text(4,106,line,TFT_YELLOW);
        }
        const auto reason=datasetMenuBlock(data,content.selected_setting);
        if(reason!=DatasetError::None) notice(datasetErrorText(reason,japanese));
        else if(data.storage.dataset_failure.save_failed) notice("RELEASE NOT SAVED",TFT_RED);
    } else if(content.page==SettingsPage::DatasetIndex) {
        const auto& view=data.datasets;const auto& progress=view.progress;
        frame_.fillRect(0,0,240,ViewHeaderBottom,TFT_BLACK);text(4,ViewHeaderTextY,view.purpose==DatasetPurpose::Verify?"VERIFY CACHE":"REBUILD CACHE");
        std::snprintf(line,sizeof(line),"TARGET %02u / ALL LANGUAGES",view.number);text(4,26,line,TFT_LIGHTGREY);
        item(4,43,view.applying?(view.purpose==DatasetPurpose::Verify?(japanese?"検証中です":"Verifying..."):(japanese?"再生成中です":"Rebuilding...")):
            view.error==DatasetError::None?(japanese?"完了しました":"Completed"):(japanese?"失敗しました":"Failed"),
            view.applying?TFT_YELLOW:view.error==DatasetError::None?TFT_GREEN:TFT_RED);
        item(4,64,indexPhaseText(progress.phase,japanese));
        frame_.drawRect(4,87,232,12,TFT_LIGHTGREY);frame_.fillRect(6,89,228*progress.percent/100,8,TFT_GREEN);
        if (!view.applying && view.error!=DatasetError::None) notice(datasetErrorText(view.error,japanese),TFT_RED);
        else {
            std::snprintf(line,sizeof(line),"%s %u%%  %lu.%03lus",progress.language,progress.percent,
                static_cast<unsigned long>(progress.elapsed_ms/1000),static_cast<unsigned long>(progress.elapsed_ms%1000));text(4,105,line);
        }
    } else if (content.page == SettingsPage::Storage) {
        const unsigned first=SettingsLayout::firstVisible(content.selected_setting,4);
        for (unsigned row = first; row < first+SettingsLayout::VisibleRows && row<4; ++row) {
            const int y = SettingsLayout::FirstRowY + (row-first) * SettingsLayout::RowStep;
            item(4, y, row == content.selected_setting ? ">" : " ", TFT_CYAN);
            item(20, y, settingsStorageLabel(language, row), row == content.selected_setting ? TFT_CYAN : TFT_WHITE);
        }
    } else if (content.page == SettingsPage::About) {
        font_.draw(frame_, 4, 27, ProductName, TextRole::Ascii8x16, TFT_WHITE);
        std::snprintf(line, sizeof(line), "VERSION %s", ApplicationVersion);
        font_.draw(frame_, 4, 47, line, TextRole::Ascii8x16, TFT_CYAN);
        std::snprintf(line, sizeof(line), "BUILD %s", BuildDate); text(4, 69, line);
        std::snprintf(line, sizeof(line), "      %s", BuildTime); text(4, 79, line);
        std::snprintf(line, sizeof(line), "GIT %s%s", BuildGitRevision, buildGitSuffix());
        font_.draw(frame_, 4, 97, line, TextRole::Ascii8x16, TFT_LIGHTGREY);
    } else if (content.page == SettingsPage::Reset) {
        item(4, 27, ">", TFT_CYAN);
        item(20, 27, label(SettingsLabel::ResetAction), TFT_CYAN);
        item(4, 57, label(SettingsLabel::ResetScope), TFT_LIGHTGREY);
        item(4, 87, label(SettingsLabel::KeepSd), TFT_LIGHTGREY);
    } else {
        for (unsigned row = 0; row < SettingsLayout::LicenseRows; ++row) {
            text(4, SettingsLayout::FirstRowY + row * SettingsLayout::LicenseRowStep,
                 licenseLine(model.licenseOffset() + row));
        }
    }
    if (!model.confirming() && content.page != SettingsPage::DatasetMenu && content.page != SettingsPage::DatasetIndex && content.page != SettingsPage::About && content.page != SettingsPage::Licenses && content.page != SettingsPage::DatasetList && content.page != SettingsPage::DatasetDetail) {
        const bool settings_error = data.storage.settings_state == SettingsState::Failed ||
                                    data.storage.settings_state == SettingsState::Unavailable ||
                                    data.storage.settings_state == SettingsState::Invalid;
        if (data.storage.busy || data.message[0] || settings_error) frame_.fillRect(0, 106, 240, 12, TFT_BLACK);
        if (data.storage.busy) text(4, 108, label(SettingsLabel::Working), TFT_YELLOW);
        else if (data.message[0]) text(4, 108, settingsMessage(language, data.message), TFT_YELLOW);
        else if (settings_error) text(4, 108, label(SettingsLabel::Error), TFT_RED);
    }
}

// Native 8x8 arrow cells; no scaled glyphs or font-dependent Unicode arrows.
void UiRenderer::drawKeyArrow(int x, int y, UiKey direction, bool enabled) {
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

void UiRenderer::drawDialogGuide(const ChoiceDialog& dialog) {
    text(4,124,"ESC DEL",TFT_WHITE);
    bool left=false,right=false;
    for(unsigned i=0;i<dialog.choices();++i)if(dialog.choiceEnabled(i)) {
        if(i<dialog.choice())left=true;
        if(i>dialog.choice())right=true;
    }
    drawKeyArrow(84,124,UiKey::Left,left);
    drawKeyArrow(96,124,UiKey::Right,right);
}

void UiRenderer::drawScreenGuide(const UiModel& model) {
    const auto content=model.snapshot();const auto& data=model.data();
    const auto screen=content.screen;
    const bool maintenance=data.boot.maintenance;
    const bool root=isScreenRoot(screen,content.page) && !model.cardInfo() && !model.editing() && !maintenance;
    const bool switching=data.storage.busy && (data.storage.operation==StorageOperation::EnterUsbMode || data.storage.operation==StorageOperation::ExitUsbMode);
    const bool accepting=content.action.kind==ActionKind::None && !switching && !(isLogBrowser(screen) && data.browse.deleting);
    const bool dataset_wait=screen==ScreenId::Settings && data.datasets.applying &&
        (content.page==SettingsPage::DatasetMenu || content.page==SettingsPage::DatasetList ||
         content.page==SettingsPage::DatasetDetail || content.page==SettingsPage::DatasetIndex);
    const bool back=accepting && !dataset_wait && !(screen==ScreenId::Settings && content.page==SettingsPage::DatasetIndex && data.storage.busy);
    const auto key_color=accepting?TFT_WHITE:TFT_DARKGREY;
    text(4,124,root && screen!=ScreenId::Menu?"ESC:MENU":"ESC DEL",back?TFT_CYAN:TFT_DARKGREY);
    if(root) {
        constexpr ScreenId targets[] = {ScreenId::LivePlace,ScreenId::Map,ScreenId::GpsLogger,ScreenId::Logs,ScreenId::Settings};
        constexpr char numbers[]="12340";
        for(unsigned i=0;i<5;++i) {
            char key[]={numbers[i],0};text(84+i*8,124,key,!accepting?TFT_DARKGREY:targets[i]==screen?TFT_CYAN:TFT_LIGHTGREY);
        }
    }
    if (!maintenance) {text(root?148:84,124,"S",key_color);text(root?164:100,124,"G",key_color);}
    bool arrows=false,up=false,down=false;const unsigned selected=content.selected_setting;
    if(screen==ScreenId::Map) {
        arrows=true;up=map::supportedScale(data.map.scale) && data.map.scale>1;
        down=map::supportedScale(data.map.scale) && data.map.scale<4;
    } else if(screen==ScreenId::Logs) {
        const auto& view=data.browse;
        arrows=true;up=!view.loading && (selected>0 || view.has_newer);
        down=!view.loading && (selected+1<view.count || view.has_older);
        const bool remove=accepting && !view.loading && selected<view.count && !(view.active_mask&(1u<<selected));
        text(180,124,"D",remove?TFT_WHITE:TFT_DARKGREY);
    } else if(screen==ScreenId::LogPoint) {
        arrows=true;up=data.browse.target>0;down=uint64_t(data.browse.target)+1<data.browse.summary.records;
    } else if(screen==ScreenId::LogDetail) {
        arrows=true;up=!data.browse.loading && selected>0;down=!data.browse.loading && selected<1;
    } else if(screen==ScreenId::Menu) {
        arrows=true;up=selected>0;down=selected+1<MenuCount;
    } else if(screen==ScreenId::Settings && !model.cardInfo()) {
        const auto page=content.page;
        if (model.editing()) {
            const bool change=accepting && !dataset_wait && (page==SettingsPage::System ||
                (page==SettingsPage::Operation && settingChoices(selected)>1) ||
                (page==SettingsPage::DatasetDetail && !data.datasets.loading && data.datasets.error==DatasetError::None && data.datasets.info.language_count>1));
            drawKeyArrow(124,124,UiKey::Left,change);drawKeyArrow(136,124,UiKey::Right,change);
        } else if (page==SettingsPage::Licenses) {
            arrows=true;const auto offset=model.licenseOffset();
            up=offset>0;down=offset+SettingsLayout::LicenseRows<licenseLineCount();
            size_t target=offset;
            if(licenseSectionCount()) {
                size_t current=0;
                while(current+1<licenseSectionCount() && licenseSectionStart(current+1)<=offset)++current;
                target=offset<licenseSectionStart(0)?0:licenseSectionStart(current?current-1:0);
            }
            bool right=false;
            for(size_t n=0;n<licenseSectionCount();++n)if(licenseSectionStart(n)>offset)right=true;
            drawKeyArrow(160,124,UiKey::Left,accepting && target!=offset);
            drawKeyArrow(172,124,UiKey::Right,accepting && right);
        } else if(page==SettingsPage::DatasetList) {
            const auto& view=data.datasets;arrows=true;
            const bool ready=!view.loading && !view.applying && view.error==DatasetError::None;
            up=ready && (selected>0 || view.has_older);
            down=ready && (selected+1<view.count || view.has_newer);
        } else if(page!=SettingsPage::About && page!=SettingsPage::Reset && page!=SettingsPage::DatasetIndex) {
            const bool ready=!dataset_wait && (page!=SettingsPage::DatasetDetail ||
                (!data.datasets.loading && data.datasets.error==DatasetError::None));
            const unsigned count=!ready?0:page==SettingsPage::Root?SettingsRootCount:
                page==SettingsPage::Operation?OperationRowCount:page==SettingsPage::System?SettingsSystemCount:
                page==SettingsPage::DatasetMenu?3:page==SettingsPage::DatasetDetail?datasetDetailRows(data.datasets,data.settings.language==UiLanguage::Japanese):4;
            arrows=true;up=ready && selected>0;down=ready && selected+1<count;
        }
    }
    if(arrows) {
        const int x=screen==ScreenId::Logs?200:root?188:124;
        drawKeyArrow(x,124,UiKey::Up,accepting && up);
        drawKeyArrow(x+12,124,UiKey::Down,accepting && down);
    }
}

void UiRenderer::drawMap(const UiModel& model) {
    const auto& data=model.data();const auto& received=model.mapFrame();const auto& image=received.image();const auto& info=received.info();
    frame_.fillRect(0,17,240,103,TFT_BLACK);
    for(unsigned y=0;y<image.height();++y)for(unsigned x=0;x<image.width();++x) {
        const auto pixel=image.get(x,y);if(pixel)frame_.drawPixel(x,y+18,pixel==2?TFT_WHITE:TFT_DARKGREY);
    }
    // Glyph-only outlines keep the entire map visible behind the labels.
    auto overlay=[&](int x,int y,const char* value,TextRole role,uint16_t color) {
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            if(dx || dy)font_.draw(frame_,x+dx,y+dy,value,role,TFT_BLACK);
        font_.draw(frame_,x,y,value,role,color);
    };
    const auto shown_scale=image.bytes()?info.request.scale:data.map.scale;
    overlay(4,22,map::scaleText(shown_scale),TextRole::UI,TFT_CYAN);
    overlay(227,22,"N",TextRole::UI,TFT_WHITE);
    frame_.fillRect(229,34,3,13,TFT_BLACK);
    frame_.fillRect(230,35,1,11,TFT_LIGHTGREY);
    for(int i=0;i<4;++i){frame_.drawPixel(230-i,35+i,TFT_LIGHTGREY);frame_.drawPixel(230+i,35+i,TFT_LIGHTGREY);}
    if(image.bytes()) {
        const double desired=50*info.meters_per_pixel,base=std::pow(10.0,std::floor(std::log10(desired)));
        double distance=base;
        for(double factor:{1.,2.,5.,10.})if(std::abs(base*factor-desired)<std::abs(distance-desired) && std::lround(base*factor/info.meters_per_pixel)<=70)distance=base*factor;
        const int length=std::max(1,int(std::lround(distance/info.meters_per_pixel))),left=44,right=left+length;
        char scale[24];if(distance>=1000)std::snprintf(scale,sizeof(scale),"%.1fkm",distance/1000);
        else if(distance>=1)std::snprintf(scale,sizeof(scale),"%.0fm",distance);
        else std::snprintf(scale,sizeof(scale),"%.3fm",distance);
        overlay(left,22,scale,TextRole::UI,TFT_WHITE);
        frame_.fillRect(left-1,33,length+3,3,TFT_BLACK);
        frame_.fillRect(left-1,30,3,9,TFT_BLACK);frame_.fillRect(right-1,30,3,9,TFT_BLACK);
        frame_.drawFastHLine(left,34,length+1,TFT_LIGHTGREY);
        frame_.fillRect(left,31,1,7,TFT_LIGHTGREY);frame_.fillRect(right,31,1,7,TFT_LIGHTGREY);
    }
    // Current-position names arrive through the same point/code/name caches as
    // Live Place. Neither an old frame nor an in-flight frame owns these rows.
    if(datasetInactive(placeIssue(data))) {
        const auto issue=datasetIssueText(placeIssue(data),data.settings.language==UiLanguage::Japanese);
        overlay(4,88,issue.first,TextRole::UI,TFT_WHITE);
        overlay(4,100,issue.second,TextRole::Place,TFT_WHITE);
    } else if(hasPlace(data) || data.place_state==PlaceState::Ready) {
        overlay(4,88,data.place[0],TextRole::UI,TFT_WHITE);
        overlay(4,100,data.place[1],TextRole::Place,TFT_WHITE);
    } else if(data.place_state==PlaceState::NotFound) {
        overlay(4,88,"NO REGION",TextRole::UI,TFT_WHITE);
    }
    if(data.map.error!=map::Error::None)
        overlay(4,48,map::errorText(data.map.error),TextRole::UI,TFT_RED);
    // Draw last so the moving marker remains visible over the two place rows.
    const auto cursor=model.mapCursor();
    const double px=info.width/2+cursor.x,py=info.height/2+cursor.y;
    if(image.bytes() && cursor.valid && px>=0 && px<image.width() && py>=0 && py<image.height()) {
        const int cx=std::lround(px),cy=18+std::lround(py);
        auto pixel=[&](int x,int y,uint16_t color) {
            if(x>=0 && x<240 && y>=18 && y<119)frame_.drawPixel(x,y,color);
        };
        const auto color=data.map.error==map::Error::None && data.gps.fix.valid && data.gps.reception==GpsReception::Fix?TFT_CYAN:TFT_DARKGREY;
        for(int y=-8;y<=8;++y)for(int x=-8;x<=8;++x)pixel(cx+x,cy+y,TFT_BLACK);
        for(int y=-5;y<=5;++y)for(int x=-5;x<=5;++x)if(std::abs(x)+std::abs(y)==5)pixel(cx+x,cy+y,color);
        for(int i=8;i<=10;++i) {pixel(cx-i,cy,color);pixel(cx+i,cy,color);pixel(cx,cy-i,color);pixel(cx,cy+i,color);}
        pixel(cx,cy,TFT_WHITE);
    }
}

}  // namespace geoputer

#include "LogPresentation.h"
#include "UiRenderer.h"
#include "LicenseText.h"
#include "SettingsLayout.h"
#include "SettingsText.h"
#include "LivePlaceLayout.h"
#include "PlacePresentation.h"
#include "DatasetPresentation.h"
#include <cstdio>

namespace geoputer {

bool UiRenderer::begin(uint8_t color_depth) {
    display_.setRotation(1);
    display_.setBrightness(102);
    return display_.width() == 240 && display_.height() == 135 &&
           frame_.begin(color_depth) && font_.begin();
}

void UiRenderer::text(int x, int y, const char* value, uint16_t color) {
    font_.draw(frame_, x, y, value, TextRole::UI, color);
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

static const char* mainError(const UiData& data) {
    if (data.storage.log.error != LogError::None) return logErrorName(data.storage.log.error);
    if (data.storage.log_result == LogError::Io || data.storage.log_result == LogError::BufferFull || data.storage.log_result == LogError::Capacity)
        return logErrorName(data.storage.log_result);
    if (data.storage.card_state != CardState::Mounted) return "SD NOT READY";
    if (datasetUnavailable(data.dataset.state)) return placeStateName(data.dataset.state);
    if (data.gps.reception == GpsReception::Unavailable) return "GPS UNAVAILABLE";
    if (data.gps.reception == GpsReception::NoData) return "GPS SIGNAL LOST";
    if (!data.gps.fix.valid) return "GPS WAITING FOR FIX";
    if (data.place_state == PlaceState::DatasetMissing) return "RG DATASET MISSING";
    if ((data.place_state == PlaceState::DataError || data.place_state == PlaceState::EncodingError || data.place_state == PlaceState::AmbiguousFiles)) return "RG LOOKUP FAILED";
    return nullptr;
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
    switch (dialog.kind()) {
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
        case DialogKind::None: return;
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
    text(4, 124, "[ENTER] CONFIRM", TFT_LIGHTGREY);
}

void UiRenderer::draw(const UiModel& model) {
    const auto content = model.snapshot();
    const auto& data = model.data();
    if (model.mode() == DisplayMode::Background) { display_.setBrightness(0); return; }
    const auto settings = validSettings(data.settings) ? data.settings : defaultSettings();
    display_.setBrightness(Brightnesses[settings.brightness] * 255 / 100);
    const auto language = settings.language;
    const bool japanese = language == UiLanguage::Japanese;
    const auto item_role = japanese ? TextRole::UiLarge : TextRole::Ascii8x16;
    auto item = [&](int x, int y, const char* value, uint16_t color = TFT_WHITE) {
        font_.draw(frame_, x, y, value, item_role, color);
    };
    frame_.setDimmed(model.confirming() && !model.datasetWarning());
    frame_.fillSprite(TFT_BLACK);
    const bool live = content.screen == ScreenId::LivePlace;
    frame_.drawFastHLine(0, live ? 16 : SettingsLayout::HeaderBottom, 240, live ? TFT_WHITE : TFT_DARKGREY);
    frame_.drawFastHLine(0, live ? LivePlaceLayout::FooterTop : 118, 240, live ? TFT_WHITE : TFT_DARKGREY);
    char line[64];
    if (content.screen == ScreenId::Boot && data.boot.fatal_dataset) {
        text(4,8,"STARTUP ERROR",TFT_RED);
        if (japanese) {
            item(4,35,"データセットが",TFT_RED);item(4,55,"見つかりません",TFT_RED);
        } else item(4,35,"Dataset not found",TFT_RED);
        std::snprintf(line,sizeof(line),"DATASET %02u",settings.dataset);text(4,84,line);
        std::snprintf(line,sizeof(line),"LANG %.24s",settings.wordbook_language[0]?settings.wordbook_language:"DEFAULT");text(4,98,line);
        text(4,124,"RESTORE SD AND RESTART",TFT_YELLOW);
        frame_.pushSprite(0,0);return;
    }
    if (content.screen == ScreenId::Boot) {
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
        std::snprintf(line,sizeof(line),"GPS:MESH  DATASET:%02u",settings.dataset);
        text(4,110,line,TFT_LIGHTGREY);
        if (!data.boot.complete) text(4, 124, "CHECKING...", TFT_CYAN);
        else {
            text(4, 124, data.boot.hasIssues() ? "WITH WARNINGS" : "READY", data.boot.hasIssues() ? TFT_YELLOW : TFT_GREEN);
            text(132, 124, "ENTER START");
        }
    } else if (content.screen == ScreenId::LivePlace) {
        const auto& gps = data.gps;
        const char* error = mainError(data);
        const bool storage_error = error != nullptr;
        const auto rec = data.recording.state;
        text(4, 4, storage_error ? "ERROR" : recordingName(rec), storage_error ? TFT_RED :
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
        text(204, 4, line, battery.percent >= 0 && battery.percent <= 20 ? battery_fill : TFT_WHITE);
        if (hasPlace(data)) {
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
        const auto detail = formatLiveGps(gps, data.place_fix);
        text(4, 62, "SPD");
        speedText(detail.speed);
        text(LivePlaceLayout::SpeedUnitX, LivePlaceLayout::SpeedUnitY, detail.speed_unit);
        text(4, 85, "HDG");
        largeText(36, 89, detail.heading);
        static const char* const labels[] = {"LAT", "LON", "ALT", "SAT"};
        const char* const values[] = {detail.latitude, detail.longitude, detail.altitude, detail.satellites};
        for (unsigned row = 0; row < 4; ++row) {
            const int y = LivePlaceLayout::DetailY + row * LivePlaceLayout::DetailStep;
            text(LivePlaceLayout::DetailLabelX, y, labels[row]);
            text(LivePlaceLayout::DetailValueX, y, values[row]);
        }
        if (error) {
            frame_.fillRect(0, LivePlaceLayout::ErrorY, 240, LivePlaceLayout::ErrorHeight, TFT_RED);
            if (data.storage.log.error != LogError::None) {
                std::snprintf(line, sizeof(line), "%s  BUF%02u/64", error, data.storage.log.buffered);
                text(4, LivePlaceLayout::LogY, line);
            } else text(4, LivePlaceLayout::LogY, error);
        } else {
            const auto seconds = data.recording.elapsed_ms / 1000;
            std::snprintf(line, sizeof(line), "LOG %02lu:%02lu:%02lu", static_cast<unsigned long>(seconds / 3600),
                          static_cast<unsigned long>(seconds / 60 % 60), static_cast<unsigned long>(seconds % 60));
            text(4, LivePlaceLayout::LogY, line, TFT_WHITE);
            std::snprintf(line, sizeof(line), "BUF %02u/64", data.storage.log.buffered); text(156, LivePlaceLayout::LogY, line, TFT_WHITE);
        }
        text(4, 124, "F1 PAUSE", rec == RecordingState::Recording ? TFT_WHITE : TFT_DARKGREY);
        text(84, 124, "1", TFT_CYAN); text(92, 124, ":GPS", TFT_LIGHTGREY);
        text(132, 124, "2", TFT_CYAN); text(140, 124, ":SET", TFT_LIGHTGREY);
        text(180, 124, "3", TFT_CYAN); text(188, 124, ":LOG", TFT_LIGHTGREY);
    } else if(isLogBrowser(content.screen)) {
        const auto& view=data.browse;
        const bool detail=content.screen==ScreenId::LogDetail;
        const bool points=content.screen==ScreenId::LogPoint;
        text(4,7,points?"LOG POINT":detail?"LOG DETAIL":"LOGS",TFT_LIGHTGREY);
        if(!points) {
            text(220,7,"SD",model.sdAccessLit()?TFT_BLUE:TFT_DARKGREY);
            if(detail && !view.loading && view.summary.session_id && view.summary.session_id==data.storage.log.session_id && data.storage.log.open)
                text(176,7,data.recording.state==RecordingState::Paused?"PAUSE":"REC",TFT_GREEN);
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
            text(4,124,view.count?"[ENTER] OPEN    [D] DELETE":"[ENTER] RELOAD",TFT_LIGHTGREY);
            text(4,124,"[ENTER]",view.loading?TFT_DARKGREY:TFT_CYAN);
            if(view.count) text(132,124,"[D]",view.loading || (view.active_mask&(1u<<content.selected_setting))?TFT_DARKGREY:TFT_CYAN);
        } else if(detail) {
            char name[30]; std::snprintf(name,sizeof(name),"%.29s",view.name); text(4,27,name);
            if(std::strlen(view.name)>29) text(4,37,view.name+29);
            char date[24];
            formatLogDateTime(view.summary.first_utc,date,sizeof(date)); std::snprintf(line,sizeof(line),"FIRST %s",date); text(4,49,line);
            formatLogDateTime(view.summary.last_utc,date,sizeof(date)); std::snprintf(line,sizeof(line),"LAST  %s",date); text(4,59,line);
            std::snprintf(line,sizeof(line),"PTS %lu %lluB",static_cast<unsigned long>(view.summary.records),static_cast<unsigned long long>(view.summary.bytes)); text(4,69,line);
            if(view.summary.tail && !view.loading) text(128,7,"TAIL",TFT_YELLOW);
            for(unsigned n=0;n<2;++n) {
                const auto color=view.loading || (!n && (!view.summary.session_id || !view.summary.records)) ? TFT_DARKGREY : content.selected_setting==n?TFT_CYAN:TFT_WHITE;
                const int y=n?101:83;
                item(4,y,content.selected_setting==n?">":" ",color);
                item(20,y,n?(japanese?"再読込":"RELOAD"):(japanese?"測位点を見る":"VIEW POINTS"),color);
            }
            text(4,124,"ESC/DEL BACK"); text(164,124,"ENTER OK");
        } else {
            std::snprintf(line,sizeof(line),"%lu/%lu",static_cast<unsigned long>(view.index+1),static_cast<unsigned long>(view.summary.records));
            text(236-static_cast<int>(std::strlen(line))*8,7,line);
            if(view.point.valid) {
                char date[24]; formatLogDateTime(view.point.fields&GpsTime?view.point.utc_epoch_ms:UINT64_MAX,date,sizeof(date));
                std::snprintf(line,sizeof(line),"%s%s",date,view.point.fields&GpsTime?" JST":""); text(4,27,line);
                std::snprintf(line,sizeof(line),"LAT %+.7f",view.point.latitude); text(4,43,line);
                std::snprintf(line,sizeof(line),"LON %+.7f",view.point.longitude); text(4,53,line);
                if(view.point.fields&GpsSpeed) std::snprintf(line,sizeof(line),"SPD %.1f km/h",view.point.speed_kmh); else std::snprintf(line,sizeof(line),"SPD --"); text(4,65,line);
                if(view.point.fields&GpsCourse) std::snprintf(line,sizeof(line),"HDG %.2f deg",view.point.course_deg); else std::snprintf(line,sizeof(line),"HDG --"); text(4,75,line);
                if(view.point.fields&GpsAltitude) std::snprintf(line,sizeof(line),"ALT %.3f m",view.point.altitude_m); else std::snprintf(line,sizeof(line),"ALT --"); text(4,85,line);
                if(view.point.fields&GpsSatellites) std::snprintf(line,sizeof(line),"SAT %u",view.point.satellites); else std::snprintf(line,sizeof(line),"SAT --"); text(4,97,line);
                if(view.point.fields&GpsHdop) std::snprintf(line,sizeof(line),"HDOP %.2f",view.point.hdop); else std::snprintf(line,sizeof(line),"HDOP --"); text(100,97,line);
            }
            text(4,124,"; PREV . NEXT ESC/DEL BACK");
        }
        if(view.loading) text(detail?132:4,points?109:detail?7:106,view.deleting?"DELETING...":"READING...",TFT_YELLOW);
        else if(view.error!=LogReadError::None && !model.confirming()) {
            if(detail) { if(view.summary.session_id && view.error==LogReadError::Corrupt) text(128,7,"CRC!",TFT_RED); else { frame_.fillRect(0,49,240,8,TFT_BLACK); text(4,49,logReadErrorName(view.error),TFT_RED); } }
            else text(4,points?109:106,logReadErrorName(view.error),TFT_RED);
        }
    } else if (content.screen == ScreenId::GpsLogger) {
        text(4, 7, "GPS LOGGER", TFT_LIGHTGREY);
        const auto& log = data.storage.log;
        const auto state = data.recording.state;
        text(164, 7, log.closing ? "CLOSING" : recordingName(state), TFT_CYAN);
        const auto seconds = data.recording.elapsed_ms / 1000;
        std::snprintf(line, sizeof(line), "LOG %02lu:%02lu:%02lu", static_cast<unsigned long>(seconds/3600),
                      static_cast<unsigned long>(seconds/60%60), static_cast<unsigned long>(seconds%60)); text(4,27,line);
        std::snprintf(line, sizeof(line), "SAVED %lu", static_cast<unsigned long>(log.saved)); text(4,41,line);
        std::snprintf(line, sizeof(line), "UNSAVED %u", log.buffered + (data.recording.pending ? 1u : 0u)); text(124,41,line);
        std::snprintf(line, sizeof(line), "BUF %u/64 LOST %lu", log.buffered, static_cast<unsigned long>(data.recording.dropped)); text(4,55,line);
        const char* operation = state == RecordingState::Stopped ? (japanese ? "開始" : "START") :
            state == RecordingState::Paused ? (japanese ? "再開" : "RESUME") : (japanese ? "停止" : "STOP");
        const auto color = log.closing ? TFT_DARKGREY : TFT_CYAN;
        item(4, 83, content.selected_setting == 0 ? ">" : " ", color);
        item(20, 83, log.closing ? (japanese ? "保存中" : "SAVING...") : operation, color);
        if (state == RecordingState::Paused) {
            const auto stop_color = content.selected_setting == 1 ? TFT_CYAN : TFT_WHITE;
            item(4, 99, content.selected_setting == 1 ? ">" : " ", stop_color);
            item(20, 99, japanese ? "停止" : "STOP", stop_color);
        }
        if (log.error != LogError::None) text(4, 69, logErrorName(log.error), TFT_RED);
        else if (data.message[0]) text(4, 69, data.message, TFT_YELLOW);
        else {
            if (log.saved) std::snprintf(line, sizeof(line), "LAST SAVE %luS", static_cast<unsigned long>((data.recording.now_ms-log.last_saved_ms)/1000));
            else std::snprintf(line, sizeof(line), "LAST SAVE --");
            text(4,69,line,TFT_LIGHTGREY);
        }
        text(4, 124, "ESC/DEL BACK"); text(164, 124, "ENTER OK");
    } else {
        const char* title = model.cardInfo() ? "CARD INFO" :
                            content.page == SettingsPage::Root ? "SETTINGS" :
                            content.page == SettingsPage::Operation ? "OPERATION" :
                            content.page == SettingsPage::Storage ? "STORAGE" :
                            content.page == SettingsPage::Reset ? "RESET" : "LICENSES";
        auto label = [&](SettingsLabel id) { return settingsLabel(language, id); };
        text(4, 7, title, TFT_LIGHTGREY);
        const char* gps_state = data.gps.reception == GpsReception::Waiting ? "WAIT" : gpsReceptionName(data.gps.reception);
        std::snprintf(line, sizeof(line), "GPS:%s", gps_state);
        text(156, 7, line, data.gps.fix.valid ? TFT_GREEN : TFT_LIGHTGREY);
        text(4, 124, model.confirming() || model.editing() ? "ESC/DEL CANCEL" : "ESC/DEL BACK");
        if (content.page == SettingsPage::DatasetList || content.page == SettingsPage::DatasetDetail) {
            const auto& view = data.datasets;
            frame_.fillRect(0,0,240,22,TFT_BLACK);text(4,7,content.page==SettingsPage::DatasetList?"DATASETS":"DATASET DETAIL",TFT_WHITE);
            if (view.loading || view.applying) {
                item(4,27,view.applying?(japanese?"適用中です":"Applying..."):(japanese?"読込中です":"Loading..."),TFT_YELLOW);
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
                if (datasetRecoverable(view.error)) text(140,124,"ENTER RETRY");
            } else if (content.page == SettingsPage::DatasetList) {
                if (!view.count) item(4,27,japanese?"候補がありません":"No datasets",TFT_YELLOW);
                for (unsigned row=0;row<view.count;++row) {
                    const int y=27+row*20;const auto color=row==content.selected_setting?TFT_CYAN:TFT_WHITE;
                    item(4,y,row==content.selected_setting?">":" ",TFT_CYAN);
                    std::snprintf(line,sizeof(line),"%02u",view.rows[row].number);
                    font_.draw(frame_,20,y,line,TextRole::Ascii8x16,color);
                    datasetSingleLine(view.rows[row].title,japanese,line,sizeof(line),24);item(44,y,line,color);
                }
                text(148,124,"ENTER OPEN");
            } else {
                const auto count=datasetDetailRows(view,japanese);
                const unsigned first=SettingsLayout::firstVisible(content.selected_setting,count);
                for (unsigned n=0;n<4 && first+n<count;++n) {
                    const unsigned row=first+n;const int y=27+n*20;
                    const auto color=row==content.selected_setting?TFT_CYAN:TFT_WHITE;
                    item(4,y,row==content.selected_setting?">":" ",TFT_CYAN);
                    const bool ascii=datasetDetailLine(view,row,japanese,line,sizeof(line));
                    font_.draw(frame_,20,y,line,ascii?TextRole::Ascii8x16:item_role,color);
                }
                std::snprintf(line,sizeof(line),"%u/%u",content.selected_setting+1u,count);text(4,110,line);
                text(140,124,model.editing()?"L/R CHANGE":content.selected_setting==count-2?"ENTER LANG":content.selected_setting==count-1?"ENTER APPLY":"UP/DOWN");
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
            text(148, 124, "ENTER BACK");
        } else if (content.page == SettingsPage::Root) {
            for (unsigned row = 0; row < 4; ++row) {
                const int y = SettingsLayout::FirstRowY + row * SettingsLayout::RowStep;
                item(4, y, row == content.selected_setting ? ">" : " ", TFT_CYAN);
                item(20, y, settingsRootLabel(language, row), row == content.selected_setting ? TFT_CYAN : TFT_WHITE);
            }
            text(148, 124, "ENTER OPEN");
        } else if (content.page == SettingsPage::Operation) {
            const unsigned first = SettingsLayout::firstVisible(content.selected_setting, SettingCount);
            for (unsigned visible = 0; visible < SettingsLayout::VisibleRows && first + visible < SettingCount; ++visible) {
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
            std::snprintf(line, sizeof(line), "%u/%u", content.selected_setting + 1u, SettingCount);
            text(4, LivePlaceLayout::LogY, line, TFT_WHITE);
            if (model.editing()) text(164, 124, "ENTER OK");
            else text(148, 124, content.selected_setting == static_cast<unsigned>(SettingId::Dataset) ? "ENTER OPEN" : !settingEditable(content.selected_setting) ? "READ ONLY" : "ENTER EDIT");
        } else if (content.page == SettingsPage::Storage) {
            for (unsigned row = 0; row < 4; ++row) {
                const int y = SettingsLayout::FirstRowY + row * SettingsLayout::RowStep;
                item(4, y, row == content.selected_setting ? ">" : " ", TFT_CYAN);
                item(20, y, settingsStorageLabel(language, row), row == content.selected_setting ? TFT_CYAN : TFT_WHITE);
            }
            text(164, 124, "ENTER OK");
        } else if (content.page == SettingsPage::Reset) {
            item(4, 27, ">", TFT_CYAN);
            item(20, 27, label(SettingsLabel::ResetAction), TFT_CYAN);
            item(4, 57, label(SettingsLabel::ResetScope), TFT_LIGHTGREY);
            item(4, 87, label(SettingsLabel::KeepSd), TFT_LIGHTGREY);
            text(164, 124, "ENTER OK");
        } else {
            for (unsigned row = 0; row < SettingsLayout::LicenseRows; ++row) {
                text(4, SettingsLayout::FirstRowY + row * SettingsLayout::LicenseRowStep,
                     licenseLine(model.licenseOffset() + row));
            }
            std::snprintf(line, sizeof(line), "%lu/%lu", static_cast<unsigned long>(model.licenseOffset() + 1), static_cast<unsigned long>(licenseLineCount()));
            text(136, 124, line, TFT_LIGHTGREY);
        }
        if (!model.confirming() && content.page != SettingsPage::Licenses && content.page != SettingsPage::DatasetList && content.page != SettingsPage::DatasetDetail) {
            const bool settings_error = data.storage.settings_state == SettingsState::Failed ||
                                        data.storage.settings_state == SettingsState::Unavailable ||
                                        data.storage.settings_state == SettingsState::Invalid;
            if (data.storage.busy || data.message[0] || settings_error) frame_.fillRect(0, 106, 240, 12, TFT_BLACK);
            if (data.storage.busy) text(4, 108, label(SettingsLabel::Working), TFT_YELLOW);
            else if (data.message[0]) text(4, 108, settingsMessage(language, data.message), TFT_YELLOW);
            else if (settings_error) text(4, 108, label(SettingsLabel::Error), TFT_RED);
        }
    }
    if (content.action.kind != ActionKind::None) {
        frame_.fillRect(0, 119, 120, 16, TFT_BLACK);
        text(4, 124, "WAIT...", TFT_CYAN);
    }
    if (model.datasetWarning()) {
        frame_.fillSprite(TFT_BLACK);
        text(4, 5, "GALUCHAT DATASET", TFT_YELLOW);
        frame_.drawFastHLine(0, 18, 240, TFT_DARKGREY);
        item(4, 28, japanese ? "使用できません" : "Dataset unavailable", TFT_YELLOW);
        if (data.dataset.state == PlaceState::TooManyChunks) {
            item(4, 56, japanese ? "チャンク数" : "Chunks");
            std::snprintf(line, sizeof(line), "%llu", static_cast<unsigned long long>(data.dataset.chunks));
            text(236 - PixelFont::width(line, TextRole::UI), 60, line);
            item(4, 80, japanese ? "上限" : "Max");
            std::snprintf(line, sizeof(line), "%lu", static_cast<unsigned long>(data.dataset.limit));
            text(236 - PixelFont::width(line, TextRole::UI), 84, line);
        } else item(4, 60, japanese ? "メモリ不足" : "Not enough memory");
        frame_.drawFastHLine(0, 118, 240, TFT_DARKGREY);
        text(4, 124, "ESC/DEL/ENTER CLOSE", TFT_LIGHTGREY);
    }
    frame_.setDimmed(false);
    if (model.confirming() && !model.datasetWarning()) drawDialog(model, language);
    frame_.pushSprite(0, 0);
}

}  // namespace geoputer

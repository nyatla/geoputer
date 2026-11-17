#include "GpsUiRenderer.h"
#include "LivePlaceLayout.h"
#include "GpsPresentation.h"
#include "PlacePresentation.h"
#include <cstdio>
#include <cstring>

namespace geoputer {

void GpsUiRenderer::drawBoot(const GpsUiModel& model, const Settings& settings) {
    const auto& data = model.data();
    char line[64];
    text(4, 8, "GEOPUTER", TFT_CYAN);
    text(76, 8, "STARTUP", TFT_LIGHTGREY);
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
    if(data.storage.dataset_failure.revision && !settings.dataset) formatDatasetRelease(data.storage.dataset_failure,line,sizeof(line));
    else if(settings.dataset)std::snprintf(line,sizeof(line),"GPS:MESH  DATASET:ON");
    else std::snprintf(line,sizeof(line),"GPS:MESH  DATASET:NONE");
    text(4,110,line,TFT_LIGHTGREY);

}

void GpsUiRenderer::drawLivePlace(const GpsUiModel& model, const UiSnapshot& content, const Settings& settings) {
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
    if(content.screen==ScreenId::Map)return;
    const auto issue = placeIssue(data);
    if (datasetInactive(issue)) {
        const auto message = datasetIssueText(issue, japanese);
        for (unsigned i = 0; i < 2; ++i)
            font_.draw(frame_, LivePlaceLayout::AddressX, LivePlaceLayout::AddressY + i * LivePlaceLayout::AddressStep,
                       i ? message.second : message.first,
                       japanese ? TextRole::Place : TextRole::Ascii8x16, TFT_WHITE);
    } else if (hasPlace(data) || data.place_state == PlaceState::Ready) {
        const auto mode = data.dataset.place_text_mode;
        if (liveAsciiWrapNeeded(data.place, mode)) {
            for (unsigned i = 0; i < 2; ++i) {
                char rows[2][config::PlaceLinePixels / 8 + EllipsisBytes];
                wrapAsciiPlaceLine(data.place[i], rows);
                for (unsigned visual = 0; visual < 2; ++visual)
                    font_.draw(frame_, LivePlaceLayout::AddressX,
                               LivePlaceLayout::AddressY + i * LivePlaceLayout::AddressStep + visual * 10,
                               rows[visual], TextRole::UI, TFT_WHITE);
            }
        } else {
            for (unsigned i = 0; i < 2; ++i) {
                char visible[config::PlaceCachedLineBytes];
                fitPlaceDisplayLine(data.place[i], visible, mode);
                font_.draw(frame_, LivePlaceLayout::AddressX, LivePlaceLayout::AddressY + i * LivePlaceLayout::AddressStep,
                           visible, placeRole(mode), TFT_WHITE);
            }
        }
    } else if (data.place_state == PlaceState::NoFix) largeText(4, 30, "WAIT GPS FIX", TFT_WHITE);
    else text(4, 30, placeStateName(data.place_state), TFT_LIGHTGREY);
    frame_.drawFastHLine(0, LivePlaceLayout::AddressBottom, 240, TFT_WHITE);
    frame_.fillRect(LivePlaceLayout::DividerX, LivePlaceLayout::AddressBottom, 1,
                    LivePlaceLayout::InfoBottom - LivePlaceLayout::AddressBottom + 1, TFT_WHITE);
    frame_.drawFastHLine(0, LivePlaceLayout::SpeedBottom, LivePlaceLayout::DividerX, TFT_WHITE);
    frame_.drawFastHLine(0, LivePlaceLayout::InfoBottom, 240, TFT_WHITE);
    const auto detail = formatGpsText(gps, datasetInactive(issue) ? GpsFix{} : data.place_fix);
    text(4, 62, "SPD");
    speedText(detail.speed);
    text(LivePlaceLayout::SpeedUnitX, LivePlaceLayout::SpeedUnitY, "km/h");
    text(4, 85, "HDG");
    largeText(84-static_cast<int>(std::strlen(detail.heading))*16,89,detail.heading);
    largeText(84,89,"°");
    char altitude[16];
    static const char* const labels[] = {"LAT", "LON", "ALT", "SAT"};
    const char* const values[] = {detail.latitude, detail.longitude, liveAltitude(detail,altitude), detail.satellites};
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

void GpsUiRenderer::drawGpsLogger(const GpsUiModel& model) {
    const auto& data = model.data();
    const auto& gps = data.gps;
    const auto& log = data.storage.log;
    char line[80];
    const auto detail = formatGpsText(gps, {}, true);
    // Two 116-pixel columns. Coordinates share a row; labels never depend on values.
    text(4,23,"LAT"); valueText(28,116,23,detail.latitude);
    text(124,23,"LON"); valueText(148,236,23,detail.longitude);
    text(4,33,"GPS"); text(36,33,gpsReceptionName(gps.reception));
    text(124,33,"RX");
    if (gps.received) std::snprintf(line,sizeof(line),"%lu",static_cast<unsigned long>((data.recording.now_ms-gps.received_at_ms)/1000));
    else std::snprintf(line,sizeof(line),"--");
    valueText(148,228,33,line);text(228,33,"s");
    text(4,43,"ALT");valueText(28,108,43,detail.altitude);text(108,43,"m");
    text(124,43,"HDOP");valueText(156,236,43,detail.hdop);
    text(4,53,"SPD");valueText(28,84,53,detail.speed);text(84,53,"km/h");
    text(124,53,"HDG");valueText(148,212,53,detail.heading);text(212,53,"deg");
    text(4,63,"SAT");valueText(28,116,63,detail.satellites);
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
}

}  // namespace geoputer

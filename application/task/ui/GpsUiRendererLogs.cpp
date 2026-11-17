#include "GpsUiRenderer.h"
#include "LogPresentation.h"
#include "GpsHeaderLayout.h"
#include "GpsPresentation.h"
#include "PlacePresentation.h"
#include "SettingsLayout.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace geoputer {

void GpsUiRenderer::drawLogBrowser(const GpsUiModel& model, const UiSnapshot& content, const Settings& settings) {
    const auto& data = model.data();
    const auto language = settings.language;
    const bool japanese = language == UiLanguage::Japanese;
    char line[64];
    const auto& view=data.browse();
    if(content.screen==ScreenId::LogProperties || content.screen==ScreenId::LogStatistics) {
        const bool statistics=content.screen==ScreenId::LogStatistics;
        text(4,SettingsLayout::HeaderTextY,statistics?"STATISTICS":"PROPERTIES",TFT_LIGHTGREY);
        char name[29];logListDisplayName(view.name,name);text(4,25,name,TFT_WHITE);
        if(statistics) {
            const auto& value=view.statistics;
            if(view.loading) {
                text(4,45,"CALCULATING...",TFT_LIGHTGREY);
                const auto total=view.statistics_total;
                frame_.drawRect(4,63,232,10,TFT_DARKGREY);
                if(total)frame_.fillRect(5,64,uint64_t(230)*std::min(value.processed,total)/total,8,TFT_CYAN);
                std::snprintf(line,sizeof(line),"%lu / %lu",static_cast<unsigned long>(value.processed),static_cast<unsigned long>(total));
                text(4,84,line);
                std::snprintf(line,sizeof(line),"%.2fs",value.elapsed_ms/1000.0);valueText(140,236,84,line);
            } else if(view.statistics_ready) {
                text(4,43,"DIST ~");std::snprintf(line,sizeof(line),"%.3f km",value.distance_m/1000);valueText(68,236,43,line);
                text(4,55,"MAX SPD");if(value.has_speed)std::snprintf(line,sizeof(line),"%.2f km/h",value.max_speed_kmh);else std::strcpy(line,"--");valueText(68,236,55,line);
                text(4,67,"MIN ALT");if(value.has_altitude)std::snprintf(line,sizeof(line),"%.2f m",value.min_altitude_m);else std::strcpy(line,"--");valueText(68,236,67,line);
                text(4,79,"MAX ALT");if(value.has_altitude)std::snprintf(line,sizeof(line),"%.2f m",value.max_altitude_m);else std::strcpy(line,"--");valueText(68,236,79,line);
                text(4,91,"POINTS");std::snprintf(line,sizeof(line),"%lu",static_cast<unsigned long>(value.processed));valueText(68,236,91,line);
                text(4,103,"SCAN");std::snprintf(line,sizeof(line),"%.3f s",value.elapsed_ms/1000.0);valueText(68,236,103,line);
            }
        } else if(!view.loading) {
            text(4,43,"POINTS");std::snprintf(line,sizeof(line),"%lu",static_cast<unsigned long>(view.summary.records));valueText(68,236,43,line);
            text(4,55,"SIZE");std::snprintf(line,sizeof(line),"%llu B",static_cast<unsigned long long>(view.summary.bytes));valueText(68,236,55,line);
            char date[24];formatLogDateTime(view.summary.first_utc,date,sizeof(date));text(4,67,"FROM");valueText(44,236,67,date);
            formatLogDateTime(view.summary.last_utc,date,sizeof(date));text(4,79,"TO");valueText(44,236,79,date);
            text(4,91,"SPAN");
            if(view.summary.first_utc!=UINT64_MAX && view.summary.last_utc!=UINT64_MAX && view.summary.last_utc>=view.summary.first_utc) {
                const auto seconds=(view.summary.last_utc-view.summary.first_utc)/1000;
                std::snprintf(line,sizeof(line),"%llu:%02u:%02u",static_cast<unsigned long long>(seconds/3600),unsigned(seconds/60%60),unsigned(seconds%60));
            } else std::strcpy(line,"--");
            valueText(44,236,91,line);text(4,103,view.summary.tail?"JST / INCOMPLETE TAIL":"JST / SAVED POINTS",view.summary.tail?TFT_YELLOW:TFT_LIGHTGREY);
        }
        if(!view.loading && view.error!=LogReadError::None) {
            if(statistics && view.error==LogReadError::Corrupt) {
                std::snprintf(line,sizeof(line),"POINT %lu",static_cast<unsigned long>(view.failed_record+1));text(4,84,line,TFT_RED);
            }
            text(4,110,logReadErrorName(view.error),TFT_RED);
        }
        return;
    }
    const bool points=content.screen==ScreenId::LogPoint;
    if(points)text(4,SettingsLayout::HeaderTextY,"POINT",TFT_LIGHTGREY);
    if(!points) {
        for(unsigned n=0;n<view.count && n<LogListRows;++n) {
            const int y=27+n*12;
            const auto color=view.loading?TFT_DARKGREY:n==content.selected_setting?TFT_CYAN:TFT_WHITE;
            text(4,y,n==content.selected_setting?">":" ",color);
            char name[29];logListDisplayName(view.names[n],name);
            text(20,y,name,color);
            if(view.active_mask&(1u<<n)) text(228,y,"R",TFT_GREEN);
        }
        if(!view.count && !view.loading && view.error==LogReadError::None) text(4,47,"NO LOG FILES",TFT_LIGHTGREY);
        const auto pages=(uint64_t(view.total_files)+LogListRows-1)/LogListRows;
        std::snprintf(line,sizeof(line),"%lu/%lu",static_cast<unsigned long>(view.count?view.page+1:0),static_cast<unsigned long>(pages));
        if(!view.loading && view.error==LogReadError::None) {
            text(4,106,line,TFT_LIGHTGREY);
            if(view.count && content.selected_setting<view.count) {
                std::snprintf(line,sizeof(line),"%lu PTS",static_cast<unsigned long>(view.list_records[content.selected_setting]));
                valueText(124,236,106,line,TFT_LIGHTGREY);
            }
        }
    } else {
        std::snprintf(line,sizeof(line),"%lu/%lu",static_cast<unsigned long>(view.summary.records?view.index+1:0),static_cast<unsigned long>(view.summary.records));
        valueText(52,gps_header::TitleRight,SettingsLayout::HeaderTextY,line);
        if(view.point.valid) {
            char date[24]; formatLogDateTime(view.point.fields&GpsTime?view.point.utc_epoch_ms:UINT64_MAX,date,sizeof(date));
            std::snprintf(line,sizeof(line),"%s%s",date,view.point.fields&GpsTime?" JST":""); text(4,27,line);
            text(4,43,"LAT");std::snprintf(line,sizeof(line),"%+.6f",view.point.latitude);valueText(28,116,43,line);
            text(124,43,"LON");std::snprintf(line,sizeof(line),"%+.6f",view.point.longitude);valueText(148,236,43,line);
            if(view.point.fields&GpsSpeed) std::snprintf(line,sizeof(line),"%.1f",view.point.speed_kmh); else std::snprintf(line,sizeof(line),"--");
            text(4,55,"SPD");valueText(28,84,55,line);text(84,55,"km/h");
            if(view.point.fields&GpsCourse) std::snprintf(line,sizeof(line),"%.1f",view.point.course_deg); else std::snprintf(line,sizeof(line),"--");
            text(124,55,"HDG");valueText(148,212,55,line);text(212,55,"deg");
            if(view.point.fields&GpsAltitude) std::snprintf(line,sizeof(line),"%.1f",view.point.altitude_m); else std::snprintf(line,sizeof(line),"--");
            text(4,65,"ALT");valueText(28,108,65,line);text(108,65,"m");
            if(view.point.fields&GpsSatellites) std::snprintf(line,sizeof(line),"%u",view.point.satellites); else std::snprintf(line,sizeof(line),"--");
            text(124,65,"SAT");valueText(148,236,65,line);
            if(view.point.fields&GpsHdop) std::snprintf(line,sizeof(line),"%.2f",view.point.hdop); else std::snprintf(line,sizeof(line),"--");
            text(4,75,"HDOP");valueText(36,116,75,line);
            if(datasetInactive(data.place_state)) {
                const auto issue=datasetIssueText(data.place_state,japanese);
                const auto role=japanese?TextRole::Place:TextRole::Ascii8x16;
                font_.draw(frame_,4,84,issue.first,role,TFT_LIGHTGREY);
                font_.draw(frame_,4,100,issue.second,role,TFT_LIGHTGREY);
            } else {
                const auto role=placeRole(data.dataset.place_text_mode);
                for (unsigned row=0;row<2;++row) {
                    char visible[config::PlaceCachedLineBytes];
                    fitPlaceDisplayLine(data.place[row],visible,data.dataset.place_text_mode);
                    font_.draw(frame_,4,row?100:84,visible,role,TFT_WHITE);
                }
                if(data.place_state==PlaceState::NotFound)font_.draw(frame_,4,84,"NO REGION",role,TFT_LIGHTGREY);
            }
        }
    }
    if(points && !view.loading && view.error==LogReadError::None && !view.summary.records)text(4,47,"NO POINTS",TFT_LIGHTGREY);
    if(view.loading && view.deleting)text(4,106,"DELETING...",TFT_YELLOW);
    if(!view.loading && view.error!=LogReadError::None && !model.confirming()) {
        text(4,points?109:106,logReadErrorName(view.error),TFT_RED);
    }
}

}  // namespace geoputer

#pragma once
#include "task/ui/UiTypes.h"

namespace geoputer {
enum class ScreenGroup : uint8_t { Common, Map, Logs, Settings, Usb };
inline ScreenGroup screenGroup(ScreenId screen) {
    if (screen==ScreenId::Map) return ScreenGroup::Map;
    if (isLogBrowser(screen)) return ScreenGroup::Logs;
    if (screen==ScreenId::Settings) return ScreenGroup::Settings;
    if (screen==ScreenId::UsbStorage) return ScreenGroup::Usb;
    return ScreenGroup::Common;
}

// Prepare a value snapshot; no task-owned buffers or pending requests change.
inline void prepareScreenView(UiData& data, ScreenId target, ScreenId previous) {
    if (isLogBrowser(target)) {
        auto& view=data.editBrowse();
        if (target==ScreenId::Logs && !isLogBrowser(previous)) {
            view={};view.summary.first_utc=view.summary.last_utc=UINT64_MAX;view.loading=true;
        } else if(target==ScreenId::Logs) {
            view.loading=false;view.error=LogReadError::None;
        } else if (target==ScreenId::LogProperties && previous==ScreenId::Logs) {
            view.point={};view.point_loaded=false;view.index=view.target=0;
            view.map_mode=view.playing=false;view.error=LogReadError::None;
            view.summary={};view.summary.first_utc=view.summary.last_utc=UINT64_MAX;view.loading=true;view.statistics_ready=false;view.statistics={};
        } else if(target==ScreenId::LogProperties || target==ScreenId::LogStatistics) {
            view.loading=false;view.error=LogReadError::None;
        } else if (target==ScreenId::LogPoint) {
            view.loading=!view.point_loaded;
            if(previous==ScreenId::LogStatistics && view.point_loaded)view.error=LogReadError::None;
        }
        if(target!=ScreenId::LogPoint) {
            view.playing=false;
            if(previous==ScreenId::LogPoint && view.point_loaded)view.target=view.index;
        }
        ++view.revision;
    } else if (target==ScreenId::Settings ? !data.hasDatasets() :
               target==ScreenId::UsbStorage ? !data.hasBrowse() : true) data.clearPage();
    ++data.revision;
}
} // namespace geoputer

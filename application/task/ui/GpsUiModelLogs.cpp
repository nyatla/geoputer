#include "GpsUiModel.h"
#include "ScreenNavigation.h"
#include "GpsControls.h"
#include <algorithm>

namespace geoputer {

bool GpsUiModel::canDeleteLog() const {
    return selection_ < data_.browse().count && !(data_.browse().active_mask & (1u << selection_));
}

void GpsUiModel::openGpsControls() {
    gps_dialog_state_ = data_.recording.state;
    dialog_.openActions(DialogKind::GpsControl, gpsControlChoices(data_), gpsControlDefault(gps_dialog_state_));
    updateDialogState();
    changed();
}

void GpsUiModel::gpsLoggerKey(UiKey key) {
    if (key == UiKey::Escape) action(ActionKind::ChangeScreen, ScreenId::LivePlace);
    else if (key == UiKey::Enter) openGpsControls();
}

void GpsUiModel::logBrowserKey(UiKey key) {
    const auto& view=data_.browse();
    if(key==UiKey::Escape) {
        action(ActionKind::ChangeScreen,screen_==ScreenId::Logs?ScreenId::LivePlace:
            screen_==ScreenId::LogProperties?ScreenId::Logs:screen_==ScreenId::LogStatistics?
            (view.statistics_from_point?ScreenId::LogPoint:ScreenId::LogProperties):ScreenId::LogProperties);
        return;
    }
    if(isLogViewScreen(screen_))for(const auto& item:LogViewShortcuts)if(key==item.key) {
        if(!selectedLogView(screen_,data_,item.action) && logViewAvailable(screen_,data_,item.action))browseAction(item.action);
        return;
    }
    if(screen_==ScreenId::LogProperties) {
        if(key==UiKey::Enter && !view.loading && view.error==LogReadError::None && view.summary.records)browseAction(BrowseAction::OpenPoint);
        return;
    }
    if(screen_==ScreenId::LogStatistics) {
        if(key==UiKey::Recalculate && !view.loading)browseAction(BrowseAction::Recalculate);
        return;
    }
    if(screen_==ScreenId::LogPoint) {
        if(!view.summary.records)return;
        if(view.map_mode && view.playing) {
            switch(key) {
                case UiKey::Up:browseAction(BrowseAction::ReplayFaster);return;
                case UiKey::Down:browseAction(BrowseAction::ReplaySlower);return;
                case UiKey::Left:browseAction(BrowseAction::ReplayReverse);return;
                case UiKey::Right:browseAction(BrowseAction::ReplayForward);return;
                default:break;
            }
        }
        if(key==UiKey::Left) browseAction(BrowseAction::Point,view.target-1);
        if(key==UiKey::Right) browseAction(BrowseAction::Point,view.target+1);
        if(key==UiKey::Up) browseAction(BrowseAction::Point,view.target>10?view.target-10:0);
        if(key==UiKey::Down) browseAction(BrowseAction::Point,
            static_cast<uint32_t>(std::min(uint64_t(view.target)+10,uint64_t(view.summary.records-1))));
        if(key==UiKey::FirstPoint) browseAction(BrowseAction::Point,0);
        if(key==UiKey::LastPoint) browseAction(BrowseAction::Point,view.summary.records-1);
        if(key==UiKey::Playback && view.map_mode) browseAction(BrowseAction::Playback);
        return;
    }
    if(view.loading) return;
    if(screen_==ScreenId::Logs) {
        if(key==UiKey::DeleteLog && selection_<view.count) {
            dialog_.open(DialogKind::DeleteLog, canDeleteLog()); changed();
        } else if(key==UiKey::Up) {
            if(selection_) { --selection_; changed(); }
            else if(view.has_newer) browseAction(BrowseAction::Newer);
        } else if(key==UiKey::Down) {
            if(selection_+1<view.count) { ++selection_; changed(); }
            else if(view.has_older) browseAction(BrowseAction::Older);
        } else if(key==UiKey::Enter) {
            if(view.count) browseAction(BrowseAction::OpenFile,selection_);
            else browseAction(BrowseAction::Reload);
        }
    }
    return;
}

void GpsUiModel::browseAction(BrowseAction kind,uint32_t index) {
    action(ActionKind::Browse);
    action_.browse_action=kind; action_.browse_index=index; action_.browse_revision=data_.browse().revision;
}

}  // namespace geoputer

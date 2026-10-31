#include "GpsApplication.h"
#include "LogReplay.h"
#include "task/ui/ScreenNavigation.h"
#include "task/storage/log/LogFormat.h"
#include "task/ui/PlacePresentation.h"
#include <cstdio>
#include <cstring>

namespace geoputer {
void GpsApplication::prepareLogScreen(ScreenId target,ScreenId previous) {
    if(browse_waiting_) browse_cancel_=true;
    if(isLogBrowser(previous) && !isLogBrowser(target))browse_release_generation_=generation_;
    if(!isLogBrowser(target))return;
    auto& logs=screenResources().logs();
    logs.need=false;logs.request={};logs.single=false;
    if(target==ScreenId::Logs && !isLogBrowser(previous)) {
        logs.request.kind=LogReadKind::List;logs.request.direction=LogListDirection::Newest;logs.need=true;
    } else if(target==ScreenId::LogProperties && previous==ScreenId::Logs) {
        logs.request.kind=LogReadKind::Summary;logs.need=true;logs.cache_count=0;
    } else if(target==ScreenId::LogPoint) {
        logs.request.kind=LogReadKind::Records;logs.need=!data_.browse().point_loaded;
    }
}

bool GpsApplication::showCachedPoint() {
    if(!screenResources().hasLogs())return false;
    auto& logs=screenResources().logs();
    auto& view=data_.editBrowse();
    if(!logs.cache_count || view.target<logs.cache_start || view.target-logs.cache_start>=logs.cache_count) return false;
    GpsFix fix{};
    if(!decodeLogRecord(logs.records[view.target-logs.cache_start],fix)) return false;
    view.point=fix; view.index=view.target;view.point_loaded=true;
    logs.next_at=taskTimeMs()+replay::intervalMs(view);
    if(gpsState().place_flow_.pending())gpsState().place_flow_.refresh();
    if(!gpsState().place_flow_.sameCell(fix,data_.dataset.unit_inv_x,data_.dataset.unit_inv_y)) {
        const auto state=!data_.settings.dataset?PlaceState::Unselected:
            datasetInactive(data_.dataset.state)?data_.dataset.state:PlaceState::Waiting;
        // A new cell invalidates the lookup, not the last completed label.
        // Keep its coordinates/region with it until the new result is committed.
        if(state==PlaceState::Waiting)data_.place_state=state;
        else clearPlacePresentation(data_,state);
    }
    view.error=LogReadError::None;view.loading=false;
    logs.need=false; ++view.revision; ++data_.revision; ui_data_dirty_=true;
    return true;
}
bool GpsApplication::operateBrowser(const UiAction& action) {
    if(!screenResources().hasLogs())return false;
    auto& logs=screenResources().logs();
    auto& view=data_.editBrowse();
    auto setPointMode=[&](bool map_mode) {
        if(view.map_mode==map_mode)return true;
        view.map_mode=map_mode;view.playing=false;
        if(gpsState().map_request_.token)gpsState().map_cancel_=true;
        ++view.revision;++data_.revision;ui_data_dirty_=true;return true;
    };
    switch(action.browse_action) {
        case BrowseAction::Delete:
            if(screen_!=ScreenId::Logs || view.loading || !action.confirmed || action.browse_revision!=view.revision || action.browse_index>=view.count) return false;
            view.selected=action.browse_index;
            if(view.active_mask&(1u<<view.selected)) {
                view.error=LogReadError::Active; ++view.revision; ++data_.revision; ui_data_dirty_=true;
                return true;
            }
            std::snprintf(view.name,sizeof(view.name),"%s",view.names[view.selected]);
            logs.request={}; logs.request.kind=LogReadKind::Delete; logs.request.confirmed=true;
            view.deleting=true;
            break;
        case BrowseAction::OpenFile:
            if(screen_!=ScreenId::Logs || view.loading || action.browse_revision!=view.revision || action.browse_index>=view.count) return false;
            view.selected=action.browse_index;
            std::snprintf(view.name,sizeof(view.name),"%s",view.names[view.selected]);
            return setScreen(ScreenId::LogProperties,action.action_id,ActionResult::Applied);
        case BrowseAction::OpenPoint: case BrowseAction::Text: case BrowseAction::Map:
            if(!logViewAvailable(screen_,data_,action.browse_action))return false;
            if(viewingLogPoint(screen_))return setPointMode(action.browse_action==BrowseAction::Map);
            view.map_mode=action.browse_action==BrowseAction::Map;view.playing=false;
            return setScreen(ScreenId::LogPoint,action.action_id,ActionResult::Applied);
        case BrowseAction::Statistics: case BrowseAction::Recalculate: {
            if((screen_!=ScreenId::LogProperties && screen_!=ScreenId::LogPoint && screen_!=ScreenId::LogStatistics) || view.loading)return false;
            if(screen_!=ScreenId::LogStatistics) {
                view.statistics_from_point=screen_==ScreenId::LogPoint;
                if(!setScreen(ScreenId::LogStatistics,action.action_id,ActionResult::Applied))return false;
            }
            if(view.statistics_ready && view.statistics_source.session_id==view.summary.session_id && action.browse_action!=BrowseAction::Recalculate)return true;
            view.statistics_ready=false;view.statistics={};view.statistics_total=view.summary.records;
            logs.request={};logs.request.kind=LogReadKind::Statistics;
            logs.request.session_id=view.summary.session_id;
            break;
        }
        case BrowseAction::Playback:
            if(!viewingLogPoint(screen_) || !view.map_mode || !view.summary.records || view.error!=LogReadError::None)return false;
            view.playing=!view.playing;logs.next_at=taskTimeMs()+replay::intervalMs(view);
            ++data_.revision;ui_data_dirty_=true;return true;
        case BrowseAction::ReplayFaster: case BrowseAction::ReplaySlower:
        case BrowseAction::ReplayForward: case BrowseAction::ReplayReverse: {
            if(!viewingLogPoint(screen_) || !view.map_mode || !view.playing)return false;
            const auto old_speed=view.replay_speed;const auto old_reverse=view.replay_reverse;
            auto speed=replay::speed(view);
            if(action.browse_action==BrowseAction::ReplayFaster && speed+1<replay::SpeedCount)++speed;
            if(action.browse_action==BrowseAction::ReplaySlower && speed>0)--speed;
            view.replay_speed=static_cast<uint8_t>(speed);
            if(action.browse_action==BrowseAction::ReplayForward)view.replay_reverse=false;
            if(action.browse_action==BrowseAction::ReplayReverse)view.replay_reverse=true;
            if(old_speed!=view.replay_speed || old_reverse!=view.replay_reverse) {
                // Keep the current point, pending reads and playback state.
                logs.next_at=taskTimeMs()+replay::intervalMs(view);
                ++data_.revision;ui_data_dirty_=true;
            }
            return true;
        }
        case BrowseAction::PausePlayback:
            if(!viewingLogPoint(screen_))return false;
            view.playing=false;++data_.revision;ui_data_dirty_=true;return true;
        case BrowseAction::Older: case BrowseAction::Newer: {
            if(screen_!=ScreenId::Logs || !view.count || view.loading) return false;
            const bool older=action.browse_action==BrowseAction::Older;
            if(older?!view.has_older:!view.has_newer) return false;
            logs.request={}; logs.request.kind=LogReadKind::List;
            logs.request.direction=older?LogListDirection::Older:LogListDirection::Newer;
            std::snprintf(logs.request.anchor,sizeof(logs.request.anchor),"%s",view.names[older?view.count-1:0]);
            break;
        }
        case BrowseAction::Reload:
            if(screen_!=ScreenId::Logs) return false;
            logs.request={}; logs.request.kind=LogReadKind::List;
            logs.request.direction=LogListDirection::Newest;
            break;
        case BrowseAction::Point:
            if(screen_!=ScreenId::LogPoint || action.browse_index>=view.summary.records) return false;
            view.playing=false;view.target=action.browse_index;
            if(showCachedPoint()) return true;
            logs.request.kind=LogReadKind::Records; logs.single=false;
            break;
    }
    view.loading=logs.need=true; view.error=LogReadError::None;
    ++data_.revision; ui_data_dirty_=true;
    return true;
}
void GpsApplication::processBrowser() {
    if(browse_cancel_ && browse_waiting_) {
        StorageRequest cancel{}; cancel.kind=MessageKind::CancelLogRead; cancel.source=TaskId::Application;
        cancel.request_id=browse_waiting_; cancel.payload.log_read.token=browse_waiting_;
        if(sendSpeculative(storage_.inbox(),cancel)==SpeculativeResult::Queued) browse_cancel_=false;
    }
    if(browse_release_generation_) {
        if(browse_waiting_)return;
        StorageRequest release{};release.kind=MessageKind::ReleaseLogBrowse;release.source=TaskId::Application;
        release.request_id=browse_release_generation_;
        release.payload.log_read.screen_generation=browse_release_generation_;
        if(sendSpeculative(storage_.inbox(),release)!=SpeculativeResult::Queued)return;
        browse_release_generation_=0;
    }
    if(!screenResources().hasLogs())return;
    auto& logs=screenResources().logs();
    if(!logs.need || gpsState().map_request_.token || gpsState().place_flow_.pending() || gpsState().place_borrow_.token || data_.storage.busy || browse_waiting_ || !isLogBrowser(screen_) || mode_!=DisplayMode::View || static_cast<int32_t>(taskTimeMs()-logs.retry_at)<0) return;
    auto read=logs.request;
    if(++browse_token_==0) ++browse_token_;
    read.token=browse_token_; read.screen_generation=generation_;
    std::snprintf(read.name,sizeof(read.name),"%s",data_.browse().name);
    if(read.kind==LogReadKind::Records) {
        if(showCachedPoint()) return;
        read.session_id=data_.browse().summary.session_id; read.limit=data_.browse().summary.records;
        read.start=logs.single?data_.browse().target:(data_.browse().target/LogReadCount)*LogReadCount;
        read.count=logs.single?1:LogReadCount;
    }
    StorageRequest request{};
    request.kind=read.kind==LogReadKind::Statistics?MessageKind::ReadLogStatistics:read.kind==LogReadKind::List?MessageKind::ListLogs:read.kind==LogReadKind::Summary?MessageKind::ReadLogSummary:read.kind==LogReadKind::Delete?MessageKind::DeleteLog:MessageKind::ReadLogRecords;
    request.payload.log_read=read;
    logs.need=false; browse_waiting_=read.token;
    SyncReturn result{};
    if(!callTask(TaskId::Storage,storage_.inbox(),request,result) || result.status!=OperationStatus::Ok) {
        browse_waiting_=0; logs.need=true; logs.retry_at=taskTimeMs()+50;
    } else applyStorage(result.storage);
}
void GpsApplication::completeBrowser(const LogReadReply& reply) {
    if(reply.request.token!=browse_waiting_) return;
    const bool transitioning=browse_cancel_;
    browse_waiting_=0; browse_cancel_=false;
    if(transitioning || !screenResources().hasLogs() || reply.request.screen_generation!=generation_ || !isLogBrowser(screen_)) return;
    auto& logs=screenResources().logs();
    auto& view=data_.editBrowse();
    if(reply.request.kind==LogReadKind::Delete && screen_==ScreenId::Logs) {
        view.deleting=false; view.error=reply.error;
        if(reply.error==LogReadError::None) {
            logs.request={}; logs.request.kind=LogReadKind::List;
            logs.request.direction=LogListDirection::FromAnchor;
            std::snprintf(logs.request.anchor,sizeof(logs.request.anchor),"%s",view.names[0]);
            view.loading=logs.need=true;
        } else view.loading=false;
    } else if(reply.request.kind==LogReadKind::List && screen_==ScreenId::Logs) {
        if(reply.error==LogReadError::None && !reply.count && reply.request.direction==LogListDirection::FromAnchor) {
            if(reply.has_newer) {
                logs.request=reply.request; logs.request.direction=LogListDirection::Newer; logs.need=true;
                return;
            }
        } else if(reply.error==LogReadError::None && !reply.count && reply.request.direction!=LogListDirection::Newest) {
            logs.request={}; logs.request.kind=LogReadKind::List;

            logs.need=true; return;
        }
        const auto rows=reply.request.count?reply.request.count:LogListRows;
        view.total_files=reply.data.list.total_files;
        view.page=reply.data.list.first_index/rows;
        std::memcpy(view.list_records,reply.data.list.records,sizeof(view.list_records));
        view.count=reply.error==LogReadError::None?reply.count:0;
        std::memcpy(view.names,reply.data.list.names,sizeof(view.names));
        view.has_older=reply.has_older; view.has_newer=reply.has_newer; view.active_mask=reply.active_mask;
        if(reply.request.direction==LogListDirection::FromAnchor) {
            if(view.selected>=view.count) view.selected=view.count?view.count-1:0;
            if(!view.count) view.page=0;
        } else view.selected=reply.request.direction==LogListDirection::Newer && view.count?view.count-1:0;
        view.error=reply.error; view.loading=false;
    } else if(reply.request.kind==LogReadKind::Summary && screen_==ScreenId::LogProperties) {
        view.summary=reply.summary;view.error=reply.error;logs.cache_count=0;
        view.loading=false;
    } else if(reply.request.kind==LogReadKind::Statistics && screen_==ScreenId::LogStatistics) {
        view.loading=false;view.error=reply.error;view.failed_record=reply.failed_record;view.statistics_ready=reply.error==LogReadError::None;
        view.statistics_source=reply.summary;view.statistics_total=reply.summary.records;
        if(view.statistics_ready)view.statistics=reply.data.statistics;
    } else if(reply.request.kind==LogReadKind::Records && screen_==ScreenId::LogPoint) {
        if(reply.error==LogReadError::None) {
            logs.cache_start=reply.request.start; logs.cache_count=reply.count;
            std::memcpy(logs.records,reply.data.records,sizeof(logs.records));
            if(showCachedPoint()) return;
            logs.need=true; return;
        }
        if(reply.error==LogReadError::Corrupt && reply.request.count>1) {
            logs.single=true; logs.need=true; return;
        }
        // An old range failure must not replace a newer navigation target.
        if(view.target<reply.request.start || view.target-reply.request.start>=reply.request.count) {
            logs.need=true; return;
        }
        view.point={}; view.index=view.target;view.point_loaded=true;view.playing=false;view.error=reply.error;view.loading=false;
        clearPlacePresentation(data_,PlaceState::NoFix);
    } else return;
    ++view.revision; ++data_.revision; ui_data_dirty_=true;
}

void GpsApplication::processReplay() {
    if(!screenResources().hasLogs() || !viewingLogPoint(screen_))return;
    auto& view=data_.editBrowse();auto& logs=screenResources().logs();
    if(!view.playing)return;
    if(view.loading || !view.point_loaded) {logs.next_at=taskTimeMs()+replay::intervalMs(view);return;}
    if(mode_!=DisplayMode::View || !view.map_mode || view.error!=LogReadError::None || data_.map.error!=map::Error::None) {
        view.playing=false;++data_.revision;ui_data_dirty_=true;return;
    }
    if(browse_waiting_ || gpsState().place_flow_.pending() || gpsState().place_borrow_.token || gpsState().map_request_.token || data_.map.busy || data_.place_state==PlaceState::Waiting || data_.place_state==PlaceState::Searching) {
        logs.next_at=taskTimeMs()+replay::intervalMs(view);return;
    }
    if(static_cast<int32_t>(taskTimeMs()-logs.next_at)<0)return;
    if(!replay::canAdvance(view)) {
        view.playing=false;++data_.revision;ui_data_dirty_=true;return;
    }
    UiAction next{};next.browse_action=BrowseAction::Point;
    next.browse_index=view.replay_reverse?view.index-1:view.index+1;
    const auto accepted=operateBrowser(next);view.playing=accepted;
    if(!accepted) {++data_.revision;ui_data_dirty_=true;}
}
}

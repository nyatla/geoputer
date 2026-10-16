#include "Application.h"
#include "task/storage/log/LogFormat.h"
#include <cstdio>
#include <cstring>

namespace geoputer {
void Application::prepareLogScreen(ScreenId target,ScreenId previous) {
    if(browse_waiting_) browse_cancel_=true;
    browse_need_=false;
    auto& view=data_.browse;
    view.loading=false; view.error=LogReadError::None;
    browse_request_={}; browse_single_=false;
    if(target==ScreenId::Logs && previous!=ScreenId::LogDetail) {
        view={}; view.summary.first_utc=view.summary.last_utc=UINT64_MAX;
        browse_request_.kind=LogReadKind::List; browse_request_.direction=LogListDirection::Newest;
        view.loading=browse_need_=true;
    } else if(target==ScreenId::LogDetail && previous==ScreenId::Logs) {
        view.summary={}; view.summary.first_utc=view.summary.last_utc=UINT64_MAX;
        browse_request_.kind=LogReadKind::Summary; view.loading=browse_need_=true;
        browse_cache_count_=0;
    } else if(target==ScreenId::LogPoint) {
        view.point={}; view.index=view.target=0;
        browse_request_.kind=LogReadKind::Records; view.loading=browse_need_=true;
        browse_cache_count_=0;
    }
    ++view.revision; ++data_.revision; ui_data_dirty_=true;
}
bool Application::showCachedPoint() {
    auto& view=data_.browse;
    if(!browse_cache_count_ || view.target<browse_cache_start_ || view.target-browse_cache_start_>=browse_cache_count_) return false;
    GpsFix fix{};
    if(!decodeLogRecord(browse_records_[view.target-browse_cache_start_],fix)) return false;
    view.point=fix; view.index=view.target; view.error=LogReadError::None; view.loading=false;
    browse_need_=false; ++view.revision; ++data_.revision; ui_data_dirty_=true;
    return true;
}
bool Application::operateBrowser(const UiAction& action) {
    auto& view=data_.browse;
    switch(action.browse_action) {
        case BrowseAction::OpenFile:
            if(screen_!=ScreenId::Logs || view.loading || action.browse_revision!=view.revision || action.browse_index>=view.count) return false;
            view.selected=action.browse_index;
            std::snprintf(view.name,sizeof(view.name),"%s",view.names[view.selected]);
            return setScreen(ScreenId::LogDetail,action.action_id,ActionResult::Applied);
        case BrowseAction::Points:
            if(screen_!=ScreenId::LogDetail || !view.summary.session_id || !view.summary.records || view.loading) return false;
            return setScreen(ScreenId::LogPoint,action.action_id,ActionResult::Applied);
        case BrowseAction::Older: case BrowseAction::Newer: {
            if(screen_!=ScreenId::Logs || !view.count || view.loading) return false;
            const bool older=action.browse_action==BrowseAction::Older;
            if(older?!view.has_older:!view.has_newer) return false;
            browse_request_={}; browse_request_.kind=LogReadKind::List;
            browse_request_.direction=older?LogListDirection::Older:LogListDirection::Newer;
            std::snprintf(browse_request_.anchor,sizeof(browse_request_.anchor),"%s",view.names[older?view.count-1:0]);
            break;
        }
        case BrowseAction::Reload:
            if(screen_!=ScreenId::Logs && screen_!=ScreenId::LogDetail) return false;
            browse_request_={}; browse_request_.kind=screen_==ScreenId::Logs?LogReadKind::List:LogReadKind::Summary;
            browse_request_.direction=LogListDirection::Newest;
            break;
        case BrowseAction::Point:
            if(screen_!=ScreenId::LogPoint || action.browse_index>=view.summary.records) return false;
            view.target=action.browse_index;
            if(showCachedPoint()) return true;
            browse_request_.kind=LogReadKind::Records; browse_single_=false;
            break;
    }
    view.loading=browse_need_=true; view.error=LogReadError::None;
    ++data_.revision; ui_data_dirty_=true;
    return true;
}
void Application::processBrowser() {
    if(browse_cancel_ && browse_waiting_) {
        TaskMessage cancel{}; cancel.kind=MessageKind::CancelLogRead; cancel.source=TaskId::Application;
        cancel.request_id=browse_waiting_; cancel.payload.log_read.token=browse_waiting_;
        if(sendSpeculative(storage_.inbox(),cancel)==SpeculativeResult::Queued) browse_cancel_=false;
    }
    if(!browse_need_ || browse_waiting_ || !isLogBrowser(screen_) || mode_!=DisplayMode::View ||
       static_cast<int32_t>(taskTimeMs()-browse_retry_at_)<0) return;
    auto read=browse_request_;
    if(++browse_token_==0) ++browse_token_;
    read.token=browse_token_; read.screen_generation=generation_;
    std::snprintf(read.name,sizeof(read.name),"%s",data_.browse.name);
    if(read.kind==LogReadKind::Records) {
        if(showCachedPoint()) return;
        read.session_id=data_.browse.summary.session_id; read.limit=data_.browse.summary.records;
        read.start=browse_single_?data_.browse.target:(data_.browse.target/LogReadCount)*LogReadCount;
        read.count=browse_single_?1:LogReadCount;
    }
    TaskMessage request{};
    request.kind=read.kind==LogReadKind::List?MessageKind::ListLogs:read.kind==LogReadKind::Summary?MessageKind::ReadLogSummary:MessageKind::ReadLogRecords;
    request.payload.log_read=read;
    browse_need_=false; browse_waiting_=read.token;
    SyncReturn result{};
    if(!callTask(TaskId::Storage,storage_.inbox(),request,result) || result.status!=OperationStatus::Ok) {
        browse_waiting_=0; browse_need_=true; browse_retry_at_=taskTimeMs()+50;
    } else applyStorage(result.storage);
}
void Application::completeBrowser(const LogReadReply& reply) {
    if(reply.request.token!=browse_waiting_) return;
    const bool transitioning=browse_cancel_;
    browse_waiting_=0; browse_cancel_=false;
    if(transitioning || reply.request.screen_generation!=generation_ || !isLogBrowser(screen_)) return;
    auto& view=data_.browse;
    if(reply.request.kind==LogReadKind::List && screen_==ScreenId::Logs) {
        if(reply.error==LogReadError::None && !reply.count && reply.request.direction!=LogListDirection::Newest) {
            browse_request_={}; browse_request_.kind=LogReadKind::List; browse_need_=true; return;
        }
        if(reply.request.direction==LogListDirection::Newest) view.page=0;
        else if(reply.request.direction==LogListDirection::Older) ++view.page;
        else if(view.page) --view.page;
        view.count=reply.error==LogReadError::None?reply.count:0;
        std::memcpy(view.names,reply.data.names,sizeof(view.names));
        view.has_older=reply.has_older; view.has_newer=reply.has_newer; view.active_mask=reply.active_mask;
        view.selected=reply.request.direction==LogListDirection::Newer && view.count?view.count-1:0;
        view.error=reply.error; view.loading=false;
    } else if(reply.request.kind==LogReadKind::Summary && screen_==ScreenId::LogDetail) {
        view.summary=reply.summary; view.error=reply.error; view.loading=false; browse_cache_count_=0;
    } else if(reply.request.kind==LogReadKind::Records && screen_==ScreenId::LogPoint) {
        if(reply.error==LogReadError::None) {
            browse_cache_start_=reply.request.start; browse_cache_count_=reply.count;
            std::memcpy(browse_records_,reply.data.records,sizeof(browse_records_));
            if(showCachedPoint()) return;
            browse_need_=true; return;
        }
        if(reply.error==LogReadError::Corrupt && reply.request.count>1) {
            browse_single_=true; browse_need_=true; return;
        }
        // An old range failure must not replace a newer navigation target.
        if(view.target<reply.request.start || view.target-reply.request.start>=reply.request.count) {
            browse_need_=true; return;
        }
        view.point={}; view.index=view.target; view.error=reply.error; view.loading=false;
    } else return;
    ++view.revision; ++data_.revision; ui_data_dirty_=true;
}
} // namespace geoputer

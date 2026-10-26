#include "utils/Console.h"
#include "Application.h"
#include "task/storage/PlaceTypes.h"
#include <cmath>

namespace geoputer {
bool Application::changeMapScale(uint8_t scale) {
    if(!screen_resources_.hasMap() || usb_mode_ || data_.boot.maintenance || !showingMap(screen_,data_) || mode_!=DisplayMode::View ||
       !map::supportedScale(scale))return false;
    auto& work=screen_resources_.map();
    if(scale==data_.map.scale)return true;
    data_.map.scale=scale;
    // A scale change is a fresh request even with the same GPS sample. Keep
    // the displayed frame until the new image has been received completely.
    work.retry_at=0;
    if(map_request_.token)map_cancel_=true;
    const auto error=mapAvailability();
    data_.map.busy=error==map::Error::None;data_.map.error=error;
    ++data_.revision;ui_data_dirty_=true;
    Console.printf("[MAP] scale=1/%u\n",unsigned(scale));
    return true;
}

map::Error Application::mapAvailability() const {
    if(data_.storage.card_state!=CardState::Mounted)return map::Error::NoCard;
    if(!data_.settings.dataset || datasetInactive(data_.dataset.state))return map::Error::NoDataset;
    const auto& fix=displayedFix(screen_,data_);
    if((viewingLogPoint(screen_) && (data_.browse().loading || data_.browse().error!=LogReadError::None)) ||
       !fix.valid || (!viewingLogPoint(screen_) && data_.gps.reception!=GpsReception::Fix) ||
       !std::isfinite(fix.latitude) || !std::isfinite(fix.longitude))return viewingLogPoint(screen_)?map::Error::InvalidInput:map::Error::NoFix;
    return map::Error::None;
}
void Application::mapState(bool busy,map::Error error) {
    if(data_.map.busy==busy && data_.map.error==error)return;
    data_.map.busy=busy;data_.map.error=error;++data_.revision;ui_data_dirty_=true;
}
void Application::completeMap(const map::FrameInfo& info) {
    if(!screen_resources_.hasMap() || map_cancel_ || !map_request_.token ||
       !map::sameRequest(info.request,map_request_) || info.request.scale!=data_.map.scale)return;
    auto& work=screen_resources_.map();
    map_session_ready_=info.error==map::Error::None;
    work.info=info;work.ready=true;
}
bool Application::mapStorageCommand(MessageKind kind,const map::Request& command) {
    StorageRequest request{};request.kind=kind;request.payload.map=command;
    SyncReturn result{};
    if(!callTask(TaskId::Storage,storage_.inbox(),request,result) || result.status!=OperationStatus::Ok)return false;
    return true;
}
bool Application::mapUiCommand(MessageKind kind,const map::FrameInfo& info,map::BufferLease* buffer) {
    UiRequest request{};request.kind=kind;request.payload.map=info;
    SyncReturn result{};
    if(!callUi(request,result) || result.status!=OperationStatus::Ok)return false;
    if(buffer)*buffer=result.map_buffer;
    return true;
}
void Application::processMap() {
    const auto now=taskTimeMs();
    const bool visible=!usb_mode_ && !data_.boot.maintenance && showingMap(screen_,data_) && mode_==DisplayMode::View;
    const auto available=mapAvailability();
    if(map_session_.token && (!visible || map_session_.screen_generation!=generation_ ||
       map_session_.dataset_generation!=data_.dataset.generation || !data_.settings.dataset))map_session_close_=true;
    if(map_request_.token && (!visible || available!=map::Error::None || map_request_.screen_generation!=generation_ ||
        map_request_.dataset_generation!=data_.dataset.generation || map_request_.scale!=data_.map.scale))map_cancel_=true;
    if(map_cancel_ && map_request_.token) {
        if(map_storage_owned_) {
            if(!mapStorageCommand(MessageKind::CancelMap,map_request_))return;
            map_storage_owned_=false;
        }
        map::FrameInfo returned{};returned.request=map_request_;
        if(!mapUiCommand(MessageKind::DiscardMapFrame,returned))return;
        map_request_={};map_cancel_=false;
        if(screen_resources_.hasMap())screen_resources_.map().ready=false;
    }
    if(map_session_close_ && map_session_.token) {
        if(!mapStorageCommand(MessageKind::CloseMapSession,map_session_))return;
        map_session_={};map_session_close_=false;map_session_ready_=false;
        if(screen_resources_.hasMap())screen_resources_.map().anchor={};
    }
    if(!visible) {mapState(false,map::Error::None);return;}
    if(!screen_resources_.hasMap())return;
    auto& work=screen_resources_.map();
    if(viewingLogPoint(screen_) && data_.browse().loading) {mapState(true,map::Error::None);return;}
    if(available!=map::Error::None) {mapState(false,available);return;}
    if(work.ready) {
        if(work.info.error!=map::Error::None) {
            mapState(false,work.info.error);if(work.info.error==map::Error::Stale)work.anchor={};
            map_cancel_=true;work.retry_at=now+config::MapViewRetryMs;return;
        }
        // Stop Storage owning the pointer before the UI can display or re-lend it.
        if(map_storage_owned_) {
            if(!mapStorageCommand(MessageKind::ReleaseMap,map_request_))return;
            map_storage_owned_=false;
        }
        if(!mapUiCommand(MessageKind::CommitMapFrame,work.info)) {
            mapState(false,map::Error::Stale);map_cancel_=true;work.retry_at=now+config::MapViewRetryMs;return;
        }
        work.anchor={work.info.request,work.info.meters_per_pixel};
        map_request_={};work.ready=false;mapState(false,map::Error::None);return;
    }
    const auto& fix=displayedFix(screen_,data_);
    const bool inside=map_session_.token && map_session_ready_ &&
        map_session_.latitude==work.anchor.request.latitude && map_session_.longitude==work.anchor.request.longitude &&
        map_session_.scale==work.anchor.request.scale && work.anchor.request.screen_generation==generation_ &&
        work.anchor.request.dataset_generation==data_.dataset.generation && work.anchor.request.scale==data_.map.scale &&
        map::inTrackingWindow(map::cursorOffset(work.anchor,fix.latitude,fix.longitude),config::MapCursorRadius);
    const bool region_known=(data_.place_state==PlaceState::Ready || data_.place_state==PlaceState::NotFound) &&
        data_.place_fix.latitude==fix.latitude && data_.place_fix.longitude==fix.longitude;
    if(!map_request_.token && inside && (!region_known || data_.place_region==work.info.region)) {
        mapState(false,map::Error::None);return;
    }
    if(map_request_.token || static_cast<int32_t>(now-work.retry_at)<0 ||
       place_flow_.pending() || place_borrow_.token || data_.storage.busy || dataset_waiting_ || dataset_check_waiting_ || browse_waiting_)return;
    if(++map_token_==0)++map_token_;
    map_request_={};map_request_.token=map_token_;map_request_.screen_generation=generation_;
    map_request_.dataset_generation=data_.dataset.generation;map_request_.sample_seq=viewingLogPoint(screen_)?data_.browse().index+1:data_.gps.sample_seq;
    map_request_.latitude=displayedFix(screen_,data_).latitude;map_request_.longitude=displayedFix(screen_,data_).longitude;
    map_request_.scale=data_.map.scale;
    map_request_.session=viewingLogPoint(screen_)?map::SessionId::Log:map::SessionId::Live;
    map_request_.region_known=region_known;map_request_.region=data_.place_region;
    if(inside) {
        map_request_.reuse=true;
        map_request_.latitude=work.anchor.request.latitude;map_request_.longitude=work.anchor.request.longitude;
    }
    map::FrameInfo loan{};loan.request=map_request_;
    if(!mapUiCommand(MessageKind::BeginMapFrame,loan,&map_request_.buffer)) {
        map_request_={};work.retry_at=now+config::MapViewRetryMs;return;
    }
    if(!mapStorageCommand(MessageKind::RequestMap,map_request_)) {
        // The operation was not accepted, but the UI loan still needs returning.
        map_cancel_=true;work.retry_at=now+config::MapViewRetryMs;return;
    }
    map_storage_owned_=true;
    if(!map_request_.reuse)map_session_ready_=false;
    map_session_=map_request_;
    mapState(true,map::Error::None);
}
} // namespace geoputer

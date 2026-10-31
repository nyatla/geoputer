#include "utils/Console.h"
#include "GpsApplication.h"
#include "task/storage/PlaceTypes.h"
#include <cmath>

namespace geoputer {
bool GpsApplication::changeMapScale(uint8_t scale) {
    if(!screenResources().hasMap() || data_.boot.maintenance || !showingMap(screen_,data_) || mode_!=DisplayMode::View || !map::supportedScale(scale))return false;
    auto& work=screenResources().map();
    if(scale==data_.map.scale)return true;
    data_.map.scale=scale;
    // A scale change is a fresh request even with the same GPS sample. Keep
    // the displayed frame until the new image has been received completely.
    work.retry_at=0;
    if(gpsState().map_request_.token)gpsState().map_cancel_=true;
    const auto error=mapAvailability();
    data_.map.busy=error==map::Error::None;data_.map.error=error;
    ++data_.revision;ui_data_dirty_=true;
    Console.printf("[MAP] scale=1/%u\n",unsigned(scale));
    return true;
}

map::Error GpsApplication::mapAvailability() const {
    if(data_.storage.card_state!=CardState::Mounted)return map::Error::NoCard;
    if(!data_.settings.dataset || datasetInactive(data_.dataset.state))return map::Error::NoDataset;
    const auto& fix=displayedFix(screen_,data_);
    if((viewingLogPoint(screen_) && (data_.browse().loading || data_.browse().error!=LogReadError::None)) || !fix.valid || (!viewingLogPoint(screen_) && data_.gps.reception!=GpsReception::Fix) || !std::isfinite(fix.latitude) || !std::isfinite(fix.longitude))return viewingLogPoint(screen_)?map::Error::InvalidInput:map::Error::NoFix;
    return map::Error::None;
}
void GpsApplication::mapState(bool busy,map::Error error) {
    if(data_.map.busy==busy && data_.map.error==error)return;
    data_.map.busy=busy;data_.map.error=error;++data_.revision;ui_data_dirty_=true;
}
void GpsApplication::completeMap(const map::FrameInfo& info) {
    if(!screenResources().hasMap() || gpsState().map_cancel_ || !gpsState().map_request_.token || !map::sameRequest(info.request,gpsState().map_request_) || info.request.scale!=data_.map.scale)return;
    auto& work=screenResources().map();
    gpsState().map_session_ready_=info.error==map::Error::None;
    work.info=info;work.ready=true;
}
bool GpsApplication::mapStorageCommand(MessageKind kind,const map::Request& command) {
    StorageRequest request{};request.kind=kind;request.payload.map=command;
    SyncReturn result{};
    if(!callTask(TaskId::Storage,storage_.inbox(),request,result) || result.status!=OperationStatus::Ok)return false;
    return true;
}
bool GpsApplication::mapUiCommand(MessageKind kind,const map::FrameInfo& info,map::BufferLease* buffer) {
    UiRequest request{};request.kind=kind;request.payload.map=info;
    SyncReturn result{};
    if(!callUi(request,result) || result.status!=OperationStatus::Ok)return false;
    if(buffer)*buffer=result.map_buffer;
    return true;
}
void GpsApplication::processMap() {
    const auto now=taskTimeMs();
    const bool visible=!data_.boot.maintenance && showingMap(screen_,data_) && mode_==DisplayMode::View;
    const auto available=mapAvailability();
    if(gpsState().map_session_.token && (!visible || gpsState().map_session_.screen_generation!=generation_ || gpsState().map_session_.dataset_generation!=data_.dataset.generation || !data_.settings.dataset))gpsState().map_session_close_=true;
    if(gpsState().map_request_.token && (!visible || available!=map::Error::None || gpsState().map_request_.screen_generation!=generation_ || gpsState().map_request_.dataset_generation!=data_.dataset.generation || gpsState().map_request_.scale!=data_.map.scale))gpsState().map_cancel_=true;
    if(gpsState().map_cancel_ && gpsState().map_request_.token) {
        if(gpsState().map_storage_owned_) {
            if(!mapStorageCommand(MessageKind::CancelMap,gpsState().map_request_))return;
            gpsState().map_storage_owned_=false;
        }
        map::FrameInfo returned{};returned.request=gpsState().map_request_;
        if(!mapUiCommand(MessageKind::DiscardMapFrame,returned))return;
        gpsState().map_request_={};gpsState().map_cancel_=false;
        if(screenResources().hasMap())screenResources().map().ready=false;
    }
    if(gpsState().map_session_close_ && gpsState().map_session_.token) {
        if(!mapStorageCommand(MessageKind::CloseMapSession,gpsState().map_session_))return;
        gpsState().map_session_={};gpsState().map_session_close_=false;gpsState().map_session_ready_=false;
        if(screenResources().hasMap())screenResources().map().anchor={};
    }
    if(!visible) {mapState(false,map::Error::None);return;}
    if(!screenResources().hasMap())return;
    auto& work=screenResources().map();
    if(viewingLogPoint(screen_) && data_.browse().loading) {mapState(true,map::Error::None);return;}
    if(available!=map::Error::None) {mapState(false,available);return;}
    if(work.ready) {
        if(work.info.error!=map::Error::None) {
            mapState(false,work.info.error);if(work.info.error==map::Error::Stale)work.anchor={};
            gpsState().map_cancel_=true;work.retry_at=now+config::MapViewRetryMs;return;
        }
        // Stop Storage owning the pointer before the UI can display or re-lend it.
        if(gpsState().map_storage_owned_) {
            if(!mapStorageCommand(MessageKind::ReleaseMap,gpsState().map_request_))return;
            gpsState().map_storage_owned_=false;
        }
        if(!mapUiCommand(MessageKind::CommitMapFrame,work.info)) {
            mapState(false,map::Error::Stale);gpsState().map_cancel_=true;work.retry_at=now+config::MapViewRetryMs;return;
        }
        work.anchor={work.info.request,work.info.meters_per_pixel};
        gpsState().map_request_={};work.ready=false;mapState(false,map::Error::None);return;
    }
    const auto& fix=displayedFix(screen_,data_);
    const bool inside=gpsState().map_session_.token && gpsState().map_session_ready_ &&
        gpsState().map_session_.latitude==work.anchor.request.latitude && gpsState().map_session_.longitude==work.anchor.request.longitude &&
        gpsState().map_session_.scale==work.anchor.request.scale && work.anchor.request.screen_generation==generation_ &&
        work.anchor.request.dataset_generation==data_.dataset.generation && work.anchor.request.scale==data_.map.scale &&
        map::inTrackingWindow(map::cursorOffset(work.anchor,fix.latitude,fix.longitude),config::MapCursorRadius);
    const bool region_known=(data_.place_state==PlaceState::Ready || data_.place_state==PlaceState::NotFound) &&
        data_.place_fix.latitude==fix.latitude && data_.place_fix.longitude==fix.longitude;
    if(!gpsState().map_request_.token && inside && (!region_known || data_.place_region==work.info.region)) {
        mapState(false,map::Error::None);return;
    }
    if(gpsState().map_request_.token || static_cast<int32_t>(now-work.retry_at)<0 || gpsState().place_flow_.pending() || gpsState().place_borrow_.token || data_.storage.busy || gpsState().dataset_waiting_ || gpsState().dataset_check_waiting_ || browse_waiting_)return;
    if(++gpsState().map_token_==0)++gpsState().map_token_;
    gpsState().map_request_={};gpsState().map_request_.token=gpsState().map_token_;gpsState().map_request_.screen_generation=generation_;
    gpsState().map_request_.dataset_generation=data_.dataset.generation;gpsState().map_request_.sample_seq=viewingLogPoint(screen_)?data_.browse().index+1:data_.gps.sample_seq;
    gpsState().map_request_.latitude=displayedFix(screen_,data_).latitude;gpsState().map_request_.longitude=displayedFix(screen_,data_).longitude;
    gpsState().map_request_.scale=data_.map.scale;
    gpsState().map_request_.session=viewingLogPoint(screen_)?map::SessionId::Log:map::SessionId::Live;
    gpsState().map_request_.region_known=region_known;gpsState().map_request_.region=data_.place_region;
    if(inside) {
        gpsState().map_request_.reuse=true;
        gpsState().map_request_.latitude=work.anchor.request.latitude;gpsState().map_request_.longitude=work.anchor.request.longitude;
    }
    map::FrameInfo loan{};loan.request=gpsState().map_request_;
    if(!mapUiCommand(MessageKind::BeginMapFrame,loan,&gpsState().map_request_.buffer)) {
        gpsState().map_request_={};work.retry_at=now+config::MapViewRetryMs;return;
    }
    if(!mapStorageCommand(MessageKind::RequestMap,gpsState().map_request_)) {
        // The operation was not accepted, but the UI loan still needs returning.
        gpsState().map_cancel_=true;work.retry_at=now+config::MapViewRetryMs;return;
    }
    gpsState().map_storage_owned_=true;
    if(!gpsState().map_request_.reuse)gpsState().map_session_ready_=false;
    gpsState().map_session_=gpsState().map_request_;
    mapState(true,map::Error::None);
}
} // namespace geoputer

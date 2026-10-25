#include "utils/Console.h"
#include "Application.h"
#include "task/storage/PlaceTypes.h"
#include <cmath>

namespace geoputer {
bool Application::changeMapScale(uint8_t scale) {
    if(!screen_resources_.hasMap() || usb_mode_ || data_.boot.maintenance || screen_!=ScreenId::Map || mode_!=DisplayMode::View ||
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
    if(!data_.gps.fix.valid || data_.gps.reception!=GpsReception::Fix ||
       !std::isfinite(data_.gps.fix.latitude) || !std::isfinite(data_.gps.fix.longitude))return map::Error::NoFix;
    return map::Error::None;
}
void Application::mapState(bool busy,map::Error error) {
    if(data_.map.busy==busy && data_.map.error==error)return;
    data_.map.busy=busy;data_.map.error=error;++data_.revision;ui_data_dirty_=true;
}
void Application::completeMap(const map::FrameInfo& info) {
    if(!screen_resources_.hasMap() || map_cancel_ || !map_request_.token || info.request.token!=map_request_.token ||
       info.request.screen_generation!=map_request_.screen_generation ||
       info.request.dataset_generation!=map_request_.dataset_generation || info.request.scale!=map_request_.scale ||
       info.request.scale!=data_.map.scale)return;
    auto& work=screen_resources_.map();
    work.info=info;work.ready=true;work.begun=false;work.row=0;
}
bool Application::mapStorageCommand(MessageKind kind,const map::Request& command) {
    StorageRequest request{};request.kind=kind;request.payload.map=command;
    SyncReturn result{};
    if(!callTask(TaskId::Storage,storage_.inbox(),request,result) || result.status!=OperationStatus::Ok)return false;
    if(kind==MessageKind::ReadMapRows)screen_resources_.map().transfer=result.map;
    return true;
}
bool Application::mapUiCommand(MessageKind kind,const map::Transfer& transfer) {
    UiRequest request{};request.kind=kind;request.payload.map=transfer;
    SyncReturn result{};return callUi(request,result) && result.status==OperationStatus::Ok;
}
void Application::processMap() {
    const auto now=taskTimeMs();
    const bool visible=!usb_mode_ && !data_.boot.maintenance && screen_==ScreenId::Map && mode_==DisplayMode::View;
    const auto available=mapAvailability();
    if(map_request_.token && (!visible || available!=map::Error::None || map_request_.screen_generation!=generation_ ||
        map_request_.dataset_generation!=data_.dataset.generation || map_request_.scale!=data_.map.scale))map_cancel_=true;
    if(map_cancel_ && map_request_.token) {
        if(!mapStorageCommand(MessageKind::CancelMap,map_request_))return;
        map_request_={};map_cancel_=false;
        if(screen_resources_.hasMap())screen_resources_.map().ready=screen_resources_.map().begun=false;
    }
    if(!visible) {mapState(false,map::Error::None);return;}
    if(!screen_resources_.hasMap())return;
    auto& work=screen_resources_.map();
    if(available!=map::Error::None) {mapState(false,available);return;}
    if(work.ready) {
        if(work.info.error!=map::Error::None) {
            mapState(false,work.info.error);map_cancel_=true;work.retry_at=now+config::MapViewRetryMs;return;
        }
        work.transfer.info=work.info;
        if(!work.begun) {
            if(!mapUiCommand(MessageKind::BeginMapFrame,work.transfer)) {
                mapState(false,map::Error::NoMemory);map_cancel_=true;work.retry_at=now+config::MapViewRetryMs;return;
            }
            work.begun=true;return;
        }
        if(work.row<work.info.height) {
            auto command=map_request_;command.row=work.row;
            if(!mapStorageCommand(MessageKind::ReadMapRows,command) ||
               !mapUiCommand(MessageKind::WriteMapRows,work.transfer)) {
                mapState(false,map::Error::Stale);map_cancel_=true;work.retry_at=now+config::MapViewRetryMs;return;
            }
            work.row+=work.transfer.rows;return;
        }
        if(!mapUiCommand(MessageKind::CommitMapFrame,work.transfer)) {
            mapState(false,map::Error::Stale);map_cancel_=true;work.retry_at=now+config::MapViewRetryMs;return;
        }
        work.anchor={work.info.request,work.info.meters_per_pixel};
        if(!mapStorageCommand(MessageKind::ReleaseMap,map_request_)) {map_cancel_=true;return;}
        map_request_={};work.ready=work.begun=false;mapState(false,map::Error::None);return;
    }
    if(!map_request_.token && work.anchor.request.screen_generation==generation_ &&
       work.anchor.request.dataset_generation==data_.dataset.generation && work.anchor.request.scale==data_.map.scale &&
       map::inTrackingWindow(map::cursorOffset(work.anchor,data_.gps.fix.latitude,data_.gps.fix.longitude),config::MapCursorRadius)) {
        mapState(false,map::Error::None);return;
    }
    if(map_request_.token || static_cast<int32_t>(now-work.retry_at)<0 ||
       place_flow_.pending() || data_.storage.busy || dataset_waiting_ || dataset_check_waiting_ || browse_waiting_)return;
    if(++map_token_==0)++map_token_;
    map_request_={};map_request_.token=map_token_;map_request_.screen_generation=generation_;
    map_request_.dataset_generation=data_.dataset.generation;map_request_.sample_seq=data_.gps.sample_seq;
    map_request_.latitude=data_.gps.fix.latitude;map_request_.longitude=data_.gps.fix.longitude;
    map_request_.scale=data_.map.scale;
    if(!mapStorageCommand(MessageKind::RequestMap,map_request_)) {
        map_request_={};work.retry_at=now+config::MapViewRetryMs;return;
    }
    mapState(true,map::Error::None);
}
} // namespace geoputer

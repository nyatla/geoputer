#include "Application.h"
#include "task/storage/PlaceTypes.h"
#include <cmath>

namespace geoputer {
map::Error Application::mapAvailability() const {
    if(data_.storage.card_state!=CardState::Mounted)return map::Error::NoCard;
    if(!data_.settings.dataset || datasetInactive(data_.dataset.state))return map::Error::NoDataset;
    if(!data_.gps.fix.valid || data_.gps.reception!=GpsReception::Fix ||
       !std::isfinite(data_.gps.fix.latitude) || !std::isfinite(data_.gps.fix.longitude))return map::Error::NoFix;
    return map::Error::None;
}
void Application::mapState(bool busy,map::Error error) {
    if(data_.map.busy==busy && data_.map.error==error)return;
    data_.map={busy,error};++data_.revision;ui_data_dirty_=true;
}
void Application::completeMap(const map::FrameInfo& info) {
    if(!map_request_.token || info.request.token!=map_request_.token ||
       info.request.screen_generation!=map_request_.screen_generation ||
       info.request.dataset_generation!=map_request_.dataset_generation)return;
    map_info_=info;map_ready_=true;map_begun_=false;map_row_=0;
}
bool Application::mapStorageCommand(MessageKind kind,const map::Request& command) {
    StorageRequest request{};request.kind=kind;request.payload.map=command;
    SyncReturn result{};
    if(!callTask(TaskId::Storage,storage_.inbox(),request,result) || result.status!=OperationStatus::Ok)return false;
    if(kind==MessageKind::ReadMapRows)map_transfer_=result.map;
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
        map_request_.dataset_generation!=data_.dataset.generation))map_cancel_=true;
    if(map_cancel_ && map_request_.token) {
        if(!mapStorageCommand(MessageKind::CancelMap,map_request_))return;
        map_request_={};map_cancel_=map_ready_=map_begun_=false;map_started_=false;
    }
    if(!visible) {mapState(false,map::Error::None);return;}
    if(available!=map::Error::None) {mapState(false,available);return;}
    if(map_ready_) {
        if(map_info_.error!=map::Error::None) {
            mapState(false,map_info_.error);map_cancel_=true;map_retry_at_=now+config::MapViewRetryMs;return;
        }
        map_transfer_.info=map_info_;
        if(!map_begun_) {
            if(!mapUiCommand(MessageKind::BeginMapFrame,map_transfer_)) {
                mapState(false,map::Error::NoMemory);map_cancel_=true;map_retry_at_=now+config::MapViewRetryMs;return;
            }
            map_begun_=true;return;
        }
        if(map_row_<map_info_.height) {
            auto command=map_request_;command.row=map_row_;
            if(!mapStorageCommand(MessageKind::ReadMapRows,command) ||
               !mapUiCommand(MessageKind::WriteMapRows,map_transfer_)) {
                mapState(false,map::Error::Stale);map_cancel_=true;map_retry_at_=now+config::MapViewRetryMs;return;
            }
            map_row_+=map_transfer_.rows;return;
        }
        if(!mapUiCommand(MessageKind::CommitMapFrame,map_transfer_)) {
            mapState(false,map::Error::Stale);map_cancel_=true;map_retry_at_=now+config::MapViewRetryMs;return;
        }
        if(!mapStorageCommand(MessageKind::ReleaseMap,map_request_)) {map_cancel_=true;return;}
        map_request_={};map_ready_=map_begun_=false;mapState(false,map::Error::None);return;
    }
    if(map_request_.token || static_cast<int32_t>(now-map_retry_at_)<0 ||
       (map_started_ && (now-map_started_at_<config::MapViewUpdateMs || data_.gps.sample_seq==map_last_sample_)) ||
       data_.storage.busy || dataset_waiting_ || dataset_check_waiting_ || browse_waiting_)return;
    if(++map_token_==0)++map_token_;
    map_request_={};map_request_.token=map_token_;map_request_.screen_generation=generation_;
    map_request_.dataset_generation=data_.dataset.generation;map_request_.sample_seq=data_.gps.sample_seq;
    map_request_.latitude=data_.gps.fix.latitude;map_request_.longitude=data_.gps.fix.longitude;
    if(!mapStorageCommand(MessageKind::RequestMap,map_request_)) {
        map_request_={};map_retry_at_=now+config::MapViewRetryMs;return;
    }
    map_started_=true;map_started_at_=now;map_last_sample_=data_.gps.sample_seq;mapState(true,map::Error::None);
}
} // namespace geoputer

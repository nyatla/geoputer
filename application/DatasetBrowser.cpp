#include "GpsApplication.h"
#include <cstring>
#include <cstdio>

namespace geoputer {
bool GpsApplication::datasetChangeReady() const {
    return datasetLogStopped(data_.recording,data_.storage.log) && !gpsState().log_pending_ && !gpsState().log_control_.action_id && !gpsState().log_accepting_;
}
bool GpsApplication::datasetUiCommand(MessageKind kind,const DatasetCompletion& completion,map::BufferLease* buffer,const IndexProgress* progress) {
    UiRequest command{};command.kind=kind;
    if(progress)command.payload.dataset_progress=*progress;else command.payload.dataset=completion;
    SyncReturn result{};
    if(!callUi(command,result) || result.status!=OperationStatus::Ok)return false;
    if(buffer)*buffer=result.map_buffer;
    return true;
}
bool GpsApplication::requestDataset(DatasetRequest request) {
    if(screen_!=ScreenId::Settings || request.purpose>DatasetPurpose::Verify || request.action>DatasetAction::Clear || !request.id.valid(true))return false;
    if(gpsState().dataset_applying_ || storage_operation_id_) {message("STORAGE BUSY");return false;}
    request.screen_generation=generation_;request.token=0;request.buffer={};
    if(datasetMutation(request.action) && !datasetChangeReady()) {
        datasetUiCommand(MessageKind::DatasetReadError,{request,DatasetError::NotStopped});message("STOP LOGGER FIRST");return false;
    }
    // Only control information is queued. A newer read supersedes the old
    // publication, but cannot revoke Storage's outstanding write loan.
    gpsState().dataset_next_=request;gpsState().dataset_pending_=true;
    if(gpsState().dataset_borrow_.token)gpsState().dataset_discard_=true;
    else gpsState().dataset_applying_=datasetMutation(request.action);
    processDataset();return true;
}
bool GpsApplication::operateDataset(const UiAction& action) {
    DatasetRequest request{};request.action=action.dataset_action;request.purpose=action.dataset_purpose;
    request.id=action.dataset_id;request.page=action.dataset_page;
    if(request.purpose>DatasetPurpose::Verify || !request.id.valid(true))return false;
    switch(request.action) {
        case DatasetAction::Open:case DatasetAction::Older:case DatasetAction::Newer:break;
        case DatasetAction::Detail:if(!request.id)return false;break;
        case DatasetAction::CancelLanguage:
            request.action=DatasetAction::Detail;
            if(!request.id || !validDatasetLanguage(action.settings.wordbook_language))return false;
            break;
        case DatasetAction::Clear:
            if(!action.confirmed || !data_.settings.dataset || request.purpose!=DatasetPurpose::Select)return false;
            break;
        case DatasetAction::RebuildIndex:case DatasetAction::VerifyIndex:
            if(!action.confirmed || !request.id || request.purpose!=(request.action==DatasetAction::RebuildIndex?DatasetPurpose::Rebuild:DatasetPurpose::Verify))return false;
            break;
        case DatasetAction::Language:case DatasetAction::Apply:
            if(!request.id || !validDatasetLanguage(action.settings.wordbook_language))return false;
            request.direction=action.browse_index?1:-1;break;
        default:return false;
    }
    std::memcpy(request.language,action.settings.wordbook_language,sizeof(request.language));
    request.language[sizeof(request.language)-1]=0;
    return requestDataset(request);
}
void GpsApplication::completeDataset(const DatasetCompletion& completion) {
    if(!gpsState().dataset_queued_ || !sameDatasetRequest(gpsState().dataset_borrow_,completion.request))return;
    // No UI synchronous call here: notifications are also drained inside a sync wait.
    gpsState().dataset_error_=completion.error;gpsState().dataset_ready_=true;
}
void GpsApplication::processDataset() {
    if(gpsState().dataset_progress_dirty_) {
        if(gpsState().dataset_borrow_.token && gpsState().dataset_borrow_.screen_generation==generation_ && screen_==ScreenId::Settings)
            datasetUiCommand(MessageKind::UpdateDatasetProgress,{},nullptr,&gpsState().dataset_progress_);
        gpsState().dataset_progress_dirty_=false;
    }
    if(gpsState().dataset_borrow_.token && gpsState().dataset_ready_) {
        const DatasetCompletion completion{gpsState().dataset_borrow_,gpsState().dataset_error_};
        bool published=false;
        if(!gpsState().dataset_discard_ && gpsState().dataset_queued_ && gpsState().dataset_borrow_.screen_generation==generation_ && screen_==ScreenId::Settings)
            published=datasetUiCommand(MessageKind::CommitDatasetRead,completion);
        if(!published && !datasetUiCommand(MessageKind::DiscardDatasetRead,completion)) {gpsState().dataset_discard_=true;return;}
        if(gpsState().dataset_queued_ && datasetMutation(gpsState().dataset_borrow_.action)) {
            if(gpsState().dataset_error_==DatasetError::None) {
                if(gpsState().dataset_borrow_.action==DatasetAction::Apply || gpsState().dataset_borrow_.action==DatasetAction::Clear) {
                    gpsState().dataset_check_needed_=true;refreshPlace();message("SETTINGS SAVED");
                } else if(data_.settings.dataset)gpsState().dataset_check_needed_=true;
            } else message(gpsState().dataset_error_==DatasetError::NotStopped?"STOP LOGGER FIRST":"DATASET CHANGE FAILED");
        }
        gpsState().dataset_borrow_={};gpsState().dataset_waiting_=0;gpsState().dataset_ready_=gpsState().dataset_queued_=gpsState().dataset_discard_=false;
        gpsState().dataset_applying_=gpsState().dataset_pending_ && datasetMutation(gpsState().dataset_next_.action);
    }
    if(gpsState().dataset_borrow_.token || !gpsState().dataset_pending_)return;
    if(screen_!=ScreenId::Settings || gpsState().dataset_next_.screen_generation!=generation_) {gpsState().dataset_pending_=gpsState().dataset_applying_=false;return;}
    if(mode_!=DisplayMode::View || data_.storage.busy || gpsState().map_request_.token || gpsState().place_borrow_.token || browse_waiting_)return;
    DatasetRequest request=gpsState().dataset_next_;
    if(datasetMutation(request.action) && !datasetChangeReady()) {
        gpsState().dataset_pending_=gpsState().dataset_applying_=false;datasetUiCommand(MessageKind::DatasetReadError,{request,DatasetError::NotStopped});return;
    }
    if(++gpsState().dataset_token_==0)++gpsState().dataset_token_;
    request.token=gpsState().dataset_token_;
    if(!datasetUiCommand(MessageKind::BeginDatasetRead,{request,DatasetError::None},&request.buffer))return;
    gpsState().dataset_pending_=false;gpsState().dataset_borrow_=request;gpsState().dataset_waiting_=request.token;
    gpsState().dataset_applying_=datasetMutation(request.action);
    gpsState().dataset_ready_=gpsState().dataset_discard_=false;gpsState().dataset_queued_=true;gpsState().dataset_error_=DatasetError::None;
    StorageRequest command{};command.kind=MessageKind::DatasetOperation;command.payload.dataset=request;
    SyncReturn result{};
    if(!callTask(TaskId::Storage,storage_.inbox(),command,result) || result.status!=OperationStatus::Ok) {
        // Rejected before writing: return the loan, then retry the same control request.
        gpsState().dataset_queued_=false;gpsState().dataset_ready_=gpsState().dataset_discard_=true;gpsState().dataset_next_=request;
        gpsState().dataset_next_.buffer={};gpsState().dataset_next_.token=0;gpsState().dataset_pending_=true;return;
    }
    applyStorage(result.storage);
}
} // namespace geoputer

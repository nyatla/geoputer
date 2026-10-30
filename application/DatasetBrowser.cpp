#include "Application.h"
#include <cstring>
#include <cstdio>

namespace geoputer {
bool Application::datasetChangeReady() const {
    return datasetLogStopped(data_.recording,data_.storage.log) && !gpsMode().log_pending_ && !gpsMode().log_control_.action_id && !gpsMode().log_accepting_;
}
bool Application::datasetUiCommand(MessageKind kind,const DatasetCompletion& completion,map::BufferLease* buffer,const IndexProgress* progress) {
    UiRequest command{};command.kind=kind;
    if(progress)command.payload.dataset_progress=*progress;else command.payload.dataset=completion;
    SyncReturn result{};
    if(!callUi(command,result) || result.status!=OperationStatus::Ok)return false;
    if(buffer)*buffer=result.map_buffer;
    return true;
}
bool Application::requestDataset(DatasetRequest request) {
    if(screen_!=ScreenId::Settings || request.purpose>DatasetPurpose::Verify || request.action>DatasetAction::Clear || request.number>99)return false;
    if(gpsMode().dataset_applying_ || storage_operation_id_) {message("STORAGE BUSY");return false;}
    request.screen_generation=generation_;request.token=0;request.buffer={};
    if(datasetMutation(request.action) && !datasetChangeReady()) {
        datasetUiCommand(MessageKind::DatasetReadError,{request,DatasetError::NotStopped});message("STOP LOGGER FIRST");return false;
    }
    // Only control information is queued. A newer read supersedes the old
    // publication, but cannot revoke Storage's outstanding write loan.
    gpsMode().dataset_next_=request;gpsMode().dataset_pending_=true;
    if(gpsMode().dataset_borrow_.token)gpsMode().dataset_discard_=true;
    else gpsMode().dataset_applying_=datasetMutation(request.action);
    processDataset();return true;
}
bool Application::operateDataset(const UiAction& action) {
    DatasetRequest request{};request.action=action.dataset_action;request.purpose=action.dataset_purpose;
    request.number=action.dataset_number;
    if(request.purpose>DatasetPurpose::Verify || request.number>99)return false;
    switch(request.action) {
        case DatasetAction::Open:case DatasetAction::Older:case DatasetAction::Newer:break;
        case DatasetAction::Detail:if(!request.number)return false;break;
        case DatasetAction::CancelLanguage:
            request.action=DatasetAction::Detail;
            if(!request.number || !validDatasetLanguage(action.settings.wordbook_language))return false;
            break;
        case DatasetAction::Clear:
            if(!action.confirmed || !data_.settings.dataset || request.purpose!=DatasetPurpose::Select)return false;
            break;
        case DatasetAction::RebuildIndex:case DatasetAction::VerifyIndex:
            if(!action.confirmed || !request.number || request.purpose!=(request.action==DatasetAction::RebuildIndex?DatasetPurpose::Rebuild:DatasetPurpose::Verify))return false;
            break;
        case DatasetAction::Language:case DatasetAction::Apply:
            if(!request.number || !validDatasetLanguage(action.settings.wordbook_language))return false;
            request.direction=action.browse_index?1:-1;break;
        default:return false;
    }
    std::memcpy(request.language,action.settings.wordbook_language,sizeof(request.language));
    request.language[sizeof(request.language)-1]=0;
    return requestDataset(request);
}
void Application::completeDataset(const DatasetCompletion& completion) {
    if(!gpsMode().dataset_queued_ || !sameDatasetRequest(gpsMode().dataset_borrow_,completion.request))return;
    // No UI synchronous call here: notifications are also drained inside a sync wait.
    gpsMode().dataset_error_=completion.error;gpsMode().dataset_ready_=true;
}
void Application::processDataset() {
    if(gpsMode().dataset_progress_dirty_) {
        if(gpsMode().dataset_borrow_.token && gpsMode().dataset_borrow_.screen_generation==generation_ && screen_==ScreenId::Settings)
            datasetUiCommand(MessageKind::UpdateDatasetProgress,{},nullptr,&gpsMode().dataset_progress_);
        gpsMode().dataset_progress_dirty_=false;
    }
    if(gpsMode().dataset_borrow_.token && gpsMode().dataset_ready_) {
        const DatasetCompletion completion{gpsMode().dataset_borrow_,gpsMode().dataset_error_};
        bool published=false;
        if(!gpsMode().dataset_discard_ && gpsMode().dataset_queued_ && gpsMode().dataset_borrow_.screen_generation==generation_ && screen_==ScreenId::Settings)
            published=datasetUiCommand(MessageKind::CommitDatasetRead,completion);
        if(!published && !datasetUiCommand(MessageKind::DiscardDatasetRead,completion)) {gpsMode().dataset_discard_=true;return;}
        if(gpsMode().dataset_queued_ && datasetMutation(gpsMode().dataset_borrow_.action)) {
            if(gpsMode().dataset_error_==DatasetError::None) {
                if(gpsMode().dataset_borrow_.action==DatasetAction::Apply || gpsMode().dataset_borrow_.action==DatasetAction::Clear) {
                    gpsMode().dataset_check_needed_=true;refreshPlace();message("SETTINGS SAVED");
                } else if(gpsMode().dataset_borrow_.number==data_.settings.dataset)gpsMode().dataset_check_needed_=true;
            } else message(gpsMode().dataset_error_==DatasetError::NotStopped?"STOP LOGGER FIRST":"DATASET CHANGE FAILED");
        }
        gpsMode().dataset_borrow_={};gpsMode().dataset_waiting_=0;gpsMode().dataset_ready_=gpsMode().dataset_queued_=gpsMode().dataset_discard_=false;
        gpsMode().dataset_applying_=gpsMode().dataset_pending_ && datasetMutation(gpsMode().dataset_next_.action);
    }
    if(gpsMode().dataset_borrow_.token || !gpsMode().dataset_pending_)return;
    if(screen_!=ScreenId::Settings || gpsMode().dataset_next_.screen_generation!=generation_) {gpsMode().dataset_pending_=gpsMode().dataset_applying_=false;return;}
    if(mode_!=DisplayMode::View || data_.storage.busy || gpsMode().map_request_.token || gpsMode().place_borrow_.token || browse_waiting_)return;
    DatasetRequest request=gpsMode().dataset_next_;
    if(datasetMutation(request.action) && !datasetChangeReady()) {
        gpsMode().dataset_pending_=gpsMode().dataset_applying_=false;datasetUiCommand(MessageKind::DatasetReadError,{request,DatasetError::NotStopped});return;
    }
    if(++gpsMode().dataset_token_==0)++gpsMode().dataset_token_;
    request.token=gpsMode().dataset_token_;
    if(!datasetUiCommand(MessageKind::BeginDatasetRead,{request,DatasetError::None},&request.buffer))return;
    gpsMode().dataset_pending_=false;gpsMode().dataset_borrow_=request;gpsMode().dataset_waiting_=request.token;
    gpsMode().dataset_applying_=datasetMutation(request.action);
    gpsMode().dataset_ready_=gpsMode().dataset_discard_=false;gpsMode().dataset_queued_=true;gpsMode().dataset_error_=DatasetError::None;
    StorageRequest command{};command.kind=MessageKind::DatasetOperation;command.payload.dataset=request;
    SyncReturn result{};
    if(!callTask(TaskId::Storage,storage_.inbox(),command,result) || result.status!=OperationStatus::Ok) {
        // Rejected before writing: return the loan, then retry the same control request.
        gpsMode().dataset_queued_=false;gpsMode().dataset_ready_=gpsMode().dataset_discard_=true;gpsMode().dataset_next_=request;
        gpsMode().dataset_next_.buffer={};gpsMode().dataset_next_.token=0;gpsMode().dataset_pending_=true;return;
    }
    applyStorage(result.storage);
}
} // namespace geoputer

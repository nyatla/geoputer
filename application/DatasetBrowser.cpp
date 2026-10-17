#include "Application.h"
#include <cstring>
#include <cstdio>

namespace geoputer {
bool Application::datasetChangeReady() const {
    return datasetLogStopped(data_.recording,data_.storage.log) && !log_pending_ && !log_control_.action_id && !log_accepting_;
}
bool Application::requestDataset(DatasetRequest request) {
    const bool applying=request.action==DatasetAction::Apply;
    // A new read/cancel may queue behind our current read. Its token supersedes
    // the old reply; the existing sync wait continues to drain notifications.
    if(dataset_applying_ || storage_operation_id_ || (applying && dataset_waiting_) ||
       (data_.storage.busy && !dataset_waiting_)) {
        message("STORAGE BUSY");return false;
    }
    if(applying && !datasetChangeReady()) {
        data_.datasets.error=DatasetError::NotStopped;
        ++data_.datasets.revision; ++data_.revision;ui_data_dirty_=true;
        message("STOP LOGGER FIRST");return false;
    }
    const bool detail=request.action!=DatasetAction::Open && request.action!=DatasetAction::Older && request.action!=DatasetAction::Newer;
    if(detail && !data_.datasets.detail) data_.datasets.info={};
    data_.datasets.detail=detail;
    if(++dataset_token_==0) ++dataset_token_;
    request.token=dataset_token_;dataset_waiting_=request.token;
    dataset_applying_=applying;
    data_.datasets.loading=true;data_.datasets.applying=applying;data_.datasets.error=DatasetError::None;
    ++data_.datasets.revision;++data_.revision;ui_data_dirty_=true;
    StorageRequest command{};command.kind=MessageKind::DatasetOperation;command.payload.dataset=request;
    SyncReturn result{};
    if(!callTask(TaskId::Storage,storage_.inbox(),command,result) || result.status!=OperationStatus::Ok) {
        dataset_waiting_=0;dataset_applying_=false;
        data_.datasets.loading=data_.datasets.applying=false;data_.datasets.error=DatasetError::Busy;
        ++data_.datasets.revision;++data_.revision;ui_data_dirty_=true;return false;
    }
    applyStorage(result.storage);
    return true;
}
bool Application::operateDataset(const UiAction& action) {
    DatasetRequest request{};request.action=action.dataset_action;
    const auto& view=data_.datasets;
    switch(action.dataset_action) {
        case DatasetAction::Open:request.number=action.browse_index? action.browse_index:1;break;
        case DatasetAction::Older:case DatasetAction::Newer:
            if(view.detail || !view.count) return false;
            request.number=view.rows[action.dataset_action==DatasetAction::Older?0:view.count-1].number;break;
        case DatasetAction::Detail:
            if(view.detail || action.browse_index>=view.count) return false;
            request.number=view.rows[action.browse_index].number;
            if(request.number==data_.settings.dataset) std::snprintf(request.language,sizeof(request.language),"%s",data_.settings.wordbook_language);
            break;
        case DatasetAction::CancelLanguage:
            if(!view.detail || !validDatasetLanguage(action.settings.wordbook_language)) return false;
            request.action=DatasetAction::Detail;request.number=view.number;
            std::strcpy(request.language,action.settings.wordbook_language);break;
        case DatasetAction::Language:case DatasetAction::Apply:
            if(!view.detail || (view.error!=DatasetError::None && view.error!=DatasetError::NotStopped && view.error!=DatasetError::SaveFailed)) return false;
            request.number=view.number;
            std::snprintf(request.language,sizeof(request.language),"%s",view.info.language);
            if(!request.language[0]) return false;
            request.direction=action.browse_index?1:-1;
            break;
    }
    return requestDataset(request);
}
void Application::completeDataset(const DatasetView& incoming) {
    if(!dataset_waiting_ || incoming.revision!=dataset_waiting_) return;
    dataset_waiting_=0;
    const bool applying=dataset_applying_;
    if(applying) {
        data_.datasets.loading=false;data_.datasets.applying=false;data_.datasets.error=incoming.error;
        if(incoming.error==DatasetError::None && incoming.restart_required) restart_pending_=true;
        else if(incoming.error==DatasetError::None) {dataset_applying_=false;message("SETTINGS SAVED");}
        else {dataset_applying_=false;message(incoming.error==DatasetError::NotStopped?"STOP LOGGER FIRST":"DATASET CHANGE FAILED");}
        ++data_.datasets.revision;
    } else {
        const auto revision=data_.datasets.revision+1;
        data_.datasets=incoming;data_.datasets.revision=revision;
    }
    ++data_.revision;ui_data_dirty_=true;
}
} // namespace geoputer

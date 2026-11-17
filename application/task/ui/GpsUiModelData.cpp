#include "GpsUiModel.h"
#include "PlacePresentation.h"
#include "DatasetPresentation.h"
#include "ScreenNavigation.h"
#include "../storage/dataset/DatasetReadMemory.h"
#include <cmath>
#include <cstring>

namespace geoputer {

OperationStatus GpsUiModel::beginDatasetRead(const DatasetRequest& request,map::BufferLease& buffer) {
    if(!request.token || request.screen_generation!=generation_ || screen_!=ScreenId::Settings || mode_!=DisplayMode::View)
        return OperationStatus::StaleContent;
    if(!request.id.valid(true) || request.purpose>DatasetPurpose::Verify || request.action>DatasetAction::Clear || request.buffer.data || request.buffer.capacity || request.buffer.id)return OperationStatus::InvalidRequest;
    if(dataset_borrow_.token || !screen_resources_.map.frame.beginWork(request.token,buffer))return OperationStatus::Unavailable;
    dataset_borrow_=request;dataset_borrow_.buffer=buffer;
    if(!validDatasetMemory(dataset_borrow_)) {
        screen_resources_.map.frame.returnWork(request.token,buffer);dataset_borrow_={};return OperationStatus::Unavailable;
    }
    *beginDatasetMemory(dataset_borrow_)={};dataset_owned_=dataset_publishable_=true;
    auto& view=data_.editDatasets();
    const bool maintenance=request.action==DatasetAction::RebuildIndex || request.action==DatasetAction::VerifyIndex;
    const bool listing=request.action==DatasetAction::Open || request.action==DatasetAction::Older || request.action==DatasetAction::Newer;
    const bool detail=!maintenance && request.action!=DatasetAction::Clear && !listing;
    if(detail && !view.detail)view.info={};
    view.detail=detail;view.purpose=request.purpose;
    if(maintenance)view.id=request.id;
    view.loading=true;view.applying=datasetMutation(request.action);view.error=DatasetError::None;
    if(view.applying){view.progress={};view.progress.verifying=request.action==DatasetAction::VerifyIndex;}
    ++view.revision;redraw_=true;return OperationStatus::Ok;
}
OperationStatus GpsUiModel::discardDatasetRead(const DatasetCompletion& completion) {
    const auto& request=completion.request;
    if(!dataset_borrow_.token || !sameDatasetRequest(dataset_borrow_,request))return OperationStatus::StaleContent;
    // Storage has completed (or rejected submission). A changed screen still returns the loan.
    endDatasetMemory(dataset_borrow_);
    if(!screen_resources_.map.frame.returnWork(request.token,request.buffer))return OperationStatus::InvalidRequest;
    dataset_borrow_={};dataset_publishable_=false;
    if(request.screen_generation==generation_ && screen_==ScreenId::Settings && dataset_owned_) {
        auto& view=data_.editDatasets();view.loading=view.applying=false;
        if(completion.error!=DatasetError::None)view.error=completion.error;
        redraw_=true;updateDialogState();
    }
    return OperationStatus::Ok;
}
OperationStatus GpsUiModel::commitDatasetRead(const DatasetCompletion& completion) {
    const auto& request=completion.request;
    if(!dataset_borrow_.token || !sameDatasetRequest(dataset_borrow_,request))return OperationStatus::StaleContent;
    if(!dataset_publishable_ || request.screen_generation!=generation_ || screen_!=ScreenId::Settings || !dataset_owned_)return OperationStatus::StaleContent;
    const auto& incoming=*datasetResultMemory(dataset_borrow_);
    if(incoming.revision!=request.token || incoming.error!=completion.error)return OperationStatus::InvalidRequest;
    auto& view=data_.editDatasets();const auto revision=view.revision+1;
    if(datasetMutation(request.action)) {
        view.error=incoming.error;view.progress=incoming.progress;view.loading=view.applying=false;
    } else view=incoming;
    view.revision=revision;datasetChanged();updateDialogState();redraw_=true;
    return discardDatasetRead(completion);
}
OperationStatus GpsUiModel::datasetReadError(const DatasetCompletion& completion) {
    if(completion.request.screen_generation!=generation_ || screen_!=ScreenId::Settings)return OperationStatus::StaleContent;
    dataset_owned_=true;auto& view=data_.editDatasets();view.error=completion.error;view.loading=view.applying=false;
    ++view.revision;redraw_=true;updateDialogState();return OperationStatus::Ok;
}
OperationStatus GpsUiModel::updateDatasetProgress(const IndexProgress& progress) {
    if(!dataset_borrow_.token || !dataset_publishable_ || progress.token!=dataset_borrow_.token || dataset_borrow_.screen_generation!=generation_ || !dataset_owned_)return OperationStatus::StaleContent;
    auto& view=data_.editDatasets();view.progress=progress;redraw_=true;return OperationStatus::Ok;
}

OperationStatus GpsUiModel::beginPlaceSearch(const PlaceRequest& request,map::BufferLease& buffer) {
    if(!request.token || request.screen_generation!=generation_ || request.dataset_generation!=data_.dataset.generation || (!request.alarm_sample && (mode_!=DisplayMode::View || (screen_!=ScreenId::LivePlace && screen_!=ScreenId::Map && !viewingLogPoint(screen_)))) || (request.alarm_sample && (data_.boot.maintenance || screen_==ScreenId::Boot)))
        return OperationStatus::StaleContent;
    if(request.buffer.data || request.buffer.id || request.buffer.capacity || request.work_bytes!=config::IndexReadBufferBytes)return OperationStatus::InvalidRequest;
    if(place_borrow_.token || !screen_resources_.map.frame.beginWork(request.token,buffer))return OperationStatus::Unavailable;
    place_borrow_=request;place_borrow_.buffer=buffer;
    if(!validPlaceMemory(place_borrow_)) {
        screen_resources_.map.frame.returnWork(request.token,buffer);place_borrow_={};return OperationStatus::Unavailable;
    }
    auto* output=beginPlaceMemory(place_borrow_);
    output->request=place_borrow_;
    return OperationStatus::Ok;
}
OperationStatus GpsUiModel::discardPlaceSearch(const PlaceRequest& request) {
    if(!place_borrow_.token || !samePlaceRequest(place_borrow_,request))return OperationStatus::StaleContent;
    // App has already observed Storage's completion (or never submitted it).
    endPlaceMemory(place_borrow_);
    if(!screen_resources_.map.frame.returnWork(request.token,request.buffer))return OperationStatus::InvalidRequest;
    place_borrow_={};return OperationStatus::Ok;
}
OperationStatus GpsUiModel::commitPlaceSearch(const PlaceRequest& request) {
    if(request.alarm_sample)return OperationStatus::InvalidRequest;
    if(!place_borrow_.token || !samePlaceRequest(place_borrow_,request))return OperationStatus::StaleContent;
    if(request.screen_generation!=generation_ || request.dataset_generation!=data_.dataset.generation || mode_!=DisplayMode::View || (screen_!=ScreenId::LivePlace && screen_!=ScreenId::Map && !viewingLogPoint(screen_)) || (viewingLogPoint(screen_) && (data_.browse().loading || request.sample_seq!=data_.browse().index+1)))
        return OperationStatus::StaleContent;
    const auto& result=*placeResultMemory(place_borrow_);
    if(!samePlaceRequest(result.request,request) || datasetInactive(result.state))return OperationStatus::InvalidRequest;
    completePlacePresentation(data_,result);redraw_=true;
    return discardPlaceSearch(request);
}

bool GpsUiModel::acceptsMap(const map::Request& request) const {
    return request.token && showingMap(screen_,data_) && mode_==DisplayMode::View && request.screen_generation==generation_ &&
        request.session==(viewingLogPoint(screen_)?map::SessionId::Log:map::SessionId::Live) &&
        request.dataset_generation==data_.dataset.generation && request.scale==data_.map.scale &&
        map::supportedScale(request.scale);
}
bool GpsUiModel::acceptsMap(const map::FrameInfo& info) const {
    return acceptsMap(info.request) && info.width==map::MaxWidth && info.height==map::MaxHeight &&
        std::isfinite(info.meters_per_pixel) && info.meters_per_pixel>0;
}
OperationStatus GpsUiModel::beginMapFrame(const map::Request& request,map::BufferLease& buffer) {
    if(!acceptsMap(request))return OperationStatus::StaleContent;
    return screen_resources_.map.frame.begin(request,buffer)?OperationStatus::Ok:OperationStatus::Unavailable;
}
OperationStatus GpsUiModel::discardMapFrame(const map::Request& request) {
    // Returning a lease is independent of the currently visible screen/mode.
    return screen_resources_.map.frame.discard(request)?OperationStatus::Ok:OperationStatus::StaleContent;
}
OperationStatus GpsUiModel::commitMapFrame(const map::FrameInfo& info) {
    if(!acceptsMap(info))return OperationStatus::StaleContent;
    if(!screen_resources_.map.frame.commit(info))return OperationStatus::InvalidRequest;
    screen_resources_.map.cursor={0,0,true};updateMapCursor();
    redraw_=true;return OperationStatus::Ok;
}

void GpsUiModel::updateMapCursor() {
    if(!screen_resources_.map.frame.image().bytes()) {screen_resources_.map.cursor={};return;}
    // Invalid GPS preserves the last known marker, drawn grey by the renderer.
    const auto& fix=displayedFix(screen_,data_);
    if(viewingLogPoint(screen_) && !fix.valid) {screen_resources_.map.cursor={};return;}
    if(!fix.valid || (!viewingLogPoint(screen_) && data_.gps.reception!=GpsReception::Fix))return;
    const auto& info=screen_resources_.map.frame.info();
    screen_resources_.map.cursor=map::cursorOffset({info.request,info.meters_per_pixel},fix.latitude,fix.longitude);
}

void GpsUiModel::datasetChanged() {
    if (screen_ == ScreenId::Settings && isDatasetBrowserPage(screen_resources_.settings.page)) {
        if (data_.datasets().detail) {
            if (screen_resources_.settings.page == SettingsPage::DatasetList) {
                selection_ = 0; screen_resources_.settings.editing = false;
                screen_resources_.settings.dataset.title_offset=0;
                screen_resources_.settings.dataset.wordbook_index=
                    static_cast<uint8_t>(data_.datasets().info.language_index);
                screen_resources_.settings.page = SettingsPage::DatasetChoice;
            }
            if (screen_resources_.settings.page == SettingsPage::DatasetDetail) {
                const auto count = datasetDetailRows(data_.datasets(), data_.settings.language == UiLanguage::Japanese);
                const auto last=count>DatasetDetailVisibleRows?count-DatasetDetailVisibleRows:0;
                if (selection_ > last) selection_ = last;
            }
        } else {
            screen_resources_.settings.page = SettingsPage::DatasetList; selection_ = data_.datasets().selected;
            screen_resources_.settings.editing = false;screen_resources_.settings.dataset.title_offset=0;
            for(unsigned i=0;i<data_.datasets().count;++i)
                if(data_.datasets().rows[i].id==screen_resources_.settings.dataset.dialog_id){selection_=i;break;}
            if (data_.datasets().count) {
                screen_resources_.settings.dataset.list_start = data_.datasets().rows[0].id;
                screen_resources_.settings.dataset.page=data_.datasets().page;
            }
        }
    }
}

}  // namespace geoputer

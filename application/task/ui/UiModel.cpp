#include "UiModel.h"
#include "PlacePresentation.h"
#include "LicenseText.h"
#include "SettingsLayout.h"
#include "ScreenNavigation.h"
#include "GpsControls.h"
#include "DatasetPresentation.h"
#include "../storage/dataset/DatasetReadMemory.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace geoputer {

static bool switchingMode(const UiData& data) {
    return data.storage.busy && (data.storage.operation == StorageOperation::EnterUsbMode ||
                                 data.storage.operation == StorageOperation::ExitUsbMode);
}

OperationStatus UiModel::setScreen(const SetUiScreen& command) {
    if (command.screen != ScreenId::LivePlace && command.screen != ScreenId::Settings && command.screen != ScreenId::Boot &&
        command.screen != ScreenId::Menu && command.screen != ScreenId::Map && command.screen != ScreenId::GpsLogger && command.screen != ScreenId::UsbStorage && !isLogBrowser(command.screen)) {
        return OperationStatus::InvalidRequest;
    }
    if (command.generation <= generation_) return OperationStatus::StaleContent;
    // Switching screens cannot silently erase an unprocessed confirmed action.
    if (action_.kind != ActionKind::None &&
        (command.processed_action_id != action_.action_id || command.action_result == ActionResult::None)) {
        return OperationStatus::InvalidRequest;
    }
    acknowledge(command.processed_action_id, command.action_result);
    if (screen_ == ScreenId::Boot || command.screen == ScreenId::Boot) last_input_ms_ = 0;
    dataset_owned_=dataset_publishable_=false;
    screen_resources_.enter(screenGroup(command.screen));
    screen_ = command.screen;
    mode_ = command.mode;
    generation_ = command.generation;
    copyData(command.data);
    selection_ = command.screen==ScreenId::Logs ? command.data.browse().selected : 0;
    dialog_.close();
    redraw_ = true;
    if (early_maintenance_key_) {
        early_maintenance_key_ = false;
        if (screen_ == ScreenId::Boot) action(ActionKind::Maintenance, ScreenId::Settings);
    }
    return OperationStatus::Ok;
}

OperationStatus UiModel::updateContent(const UpdateUiContent& command) {
    if (command.generation != generation_ || command.data.revision < data_.revision) {
        return OperationStatus::StaleContent;
    }
    copyData(command.data);
    mode_ = command.mode;
    acknowledge(command.processed_action_id, command.action_result);
    redraw_ = true;
    return OperationStatus::Ok;
}

OperationStatus UiModel::beginDatasetRead(const DatasetRequest& request,map::BufferLease& buffer) {
    if(!request.token || request.screen_generation!=generation_ || screen_!=ScreenId::Settings || mode_!=DisplayMode::View)
        return OperationStatus::StaleContent;
    if(request.number>99 || request.purpose>DatasetPurpose::Verify || request.action>DatasetAction::Clear ||
       request.buffer.data || request.buffer.capacity || request.buffer.id)return OperationStatus::InvalidRequest;
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
    if(maintenance)view.number=request.number;
    view.loading=true;view.applying=datasetMutation(request.action);view.error=DatasetError::None;
    if(view.applying){view.progress={};view.progress.verifying=request.action==DatasetAction::VerifyIndex;}
    ++view.revision;redraw_=true;return OperationStatus::Ok;
}
OperationStatus UiModel::discardDatasetRead(const DatasetCompletion& completion) {
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
OperationStatus UiModel::commitDatasetRead(const DatasetCompletion& completion) {
    const auto& request=completion.request;
    if(!dataset_borrow_.token || !sameDatasetRequest(dataset_borrow_,request))return OperationStatus::StaleContent;
    if(!dataset_publishable_ || request.screen_generation!=generation_ || screen_!=ScreenId::Settings ||
       !dataset_owned_)return OperationStatus::StaleContent;
    const auto& incoming=*datasetResultMemory(dataset_borrow_);
    if(incoming.revision!=request.token || incoming.error!=completion.error)return OperationStatus::InvalidRequest;
    auto& view=data_.editDatasets();const auto revision=view.revision+1;
    if(datasetMutation(request.action)) {
        view.error=incoming.error;view.progress=incoming.progress;view.loading=view.applying=false;
    } else view=incoming;
    view.revision=revision;datasetChanged();updateDialogState();redraw_=true;
    return discardDatasetRead(completion);
}
OperationStatus UiModel::datasetReadError(const DatasetCompletion& completion) {
    if(completion.request.screen_generation!=generation_ || screen_!=ScreenId::Settings)return OperationStatus::StaleContent;
    dataset_owned_=true;auto& view=data_.editDatasets();view.error=completion.error;view.loading=view.applying=false;
    ++view.revision;redraw_=true;updateDialogState();return OperationStatus::Ok;
}
OperationStatus UiModel::updateDatasetProgress(const IndexProgress& progress) {
    if(!dataset_borrow_.token || !dataset_publishable_ || progress.token!=dataset_borrow_.token ||
       dataset_borrow_.screen_generation!=generation_ || !dataset_owned_)return OperationStatus::StaleContent;
    auto& view=data_.editDatasets();view.progress=progress;redraw_=true;return OperationStatus::Ok;
}

OperationStatus UiModel::beginPlaceSearch(const PlaceRequest& request,map::BufferLease& buffer) {
    if(!request.token || request.screen_generation!=generation_ || request.dataset_generation!=data_.dataset.generation ||
       mode_!=DisplayMode::View || (screen_!=ScreenId::LivePlace && screen_!=ScreenId::Map && !viewingLogPoint(screen_)))
        return OperationStatus::StaleContent;
    if(request.buffer.data || request.buffer.id || request.buffer.capacity ||
       request.work_bytes!=config::IndexReadBufferBytes)return OperationStatus::InvalidRequest;
    if(place_borrow_.token || !screen_resources_.map.frame.beginWork(request.token,buffer))return OperationStatus::Unavailable;
    place_borrow_=request;place_borrow_.buffer=buffer;
    if(!validPlaceMemory(place_borrow_)) {
        screen_resources_.map.frame.returnWork(request.token,buffer);place_borrow_={};return OperationStatus::Unavailable;
    }
    auto* output=beginPlaceMemory(place_borrow_);
    output->request=place_borrow_;
    return OperationStatus::Ok;
}
OperationStatus UiModel::discardPlaceSearch(const PlaceRequest& request) {
    if(!place_borrow_.token || !samePlaceRequest(place_borrow_,request))return OperationStatus::StaleContent;
    // App has already observed Storage's completion (or never submitted it).
    endPlaceMemory(place_borrow_);
    if(!screen_resources_.map.frame.returnWork(request.token,request.buffer))return OperationStatus::InvalidRequest;
    place_borrow_={};return OperationStatus::Ok;
}
OperationStatus UiModel::commitPlaceSearch(const PlaceRequest& request) {
    if(!place_borrow_.token || !samePlaceRequest(place_borrow_,request))return OperationStatus::StaleContent;
    if(request.screen_generation!=generation_ || request.dataset_generation!=data_.dataset.generation ||
       mode_!=DisplayMode::View || (screen_!=ScreenId::LivePlace && screen_!=ScreenId::Map && !viewingLogPoint(screen_)) ||
       (viewingLogPoint(screen_) && (data_.browse().loading || request.sample_seq!=data_.browse().index+1)))
        return OperationStatus::StaleContent;
    const auto& result=*placeResultMemory(place_borrow_);
    if(!samePlaceRequest(result.request,request) || datasetInactive(result.state))return OperationStatus::InvalidRequest;
    completePlacePresentation(data_,result);redraw_=true;
    return discardPlaceSearch(request);
}

bool UiModel::acceptsMap(const map::Request& request) const {
    return request.token && showingMap(screen_,data_) && mode_==DisplayMode::View && request.screen_generation==generation_ &&
        request.session==(viewingLogPoint(screen_)?map::SessionId::Log:map::SessionId::Live) &&
        request.dataset_generation==data_.dataset.generation && request.scale==data_.map.scale &&
        map::supportedScale(request.scale);
}
bool UiModel::acceptsMap(const map::FrameInfo& info) const {
    return acceptsMap(info.request) && info.width==map::MaxWidth && info.height==map::MaxHeight &&
        std::isfinite(info.meters_per_pixel) && info.meters_per_pixel>0;
}
OperationStatus UiModel::beginMapFrame(const map::Request& request,map::BufferLease& buffer) {
    if(!acceptsMap(request))return OperationStatus::StaleContent;
    return screen_resources_.map.frame.begin(request,buffer)?OperationStatus::Ok:OperationStatus::Unavailable;
}
OperationStatus UiModel::discardMapFrame(const map::Request& request) {
    // Returning a lease is independent of the currently visible screen/mode.
    return screen_resources_.map.frame.discard(request)?OperationStatus::Ok:OperationStatus::StaleContent;
}
OperationStatus UiModel::commitMapFrame(const map::FrameInfo& info) {
    if(!acceptsMap(info))return OperationStatus::StaleContent;
    if(!screen_resources_.map.frame.commit(info))return OperationStatus::InvalidRequest;
    screen_resources_.map.cursor={0,0,true};updateMapCursor();
    redraw_=true;return OperationStatus::Ok;
}

void UiModel::updateMapCursor() {
    if(!screen_resources_.map.frame.image().bytes()) {screen_resources_.map.cursor={};return;}
    // Invalid GPS preserves the last known marker, drawn grey by the renderer.
    const auto& fix=displayedFix(screen_,data_);
    if(viewingLogPoint(screen_) && !fix.valid) {screen_resources_.map.cursor={};return;}
    if(!fix.valid || (!viewingLogPoint(screen_) && data_.gps.reception!=GpsReception::Fix))return;
    const auto& info=screen_resources_.map.frame.info();
    screen_resources_.map.cursor=map::cursorOffset({info.request,info.meters_per_pixel},fix.latitude,fix.longitude);
}

void UiModel::copyData(const UiData& data) {
    if (data.gps.fix.valid) clock_.setInitialUtc(data.gps.fix.utc_ms, last_tick_ms_);
    const auto previous_sd = data_.storage.sd;
    const auto previous_rg = data_.storage.rg;
    if (newerRevision(data.storage.sd.revision, previous_sd.revision) &&
        (!sd_seen_ || data.storage.sd.access_id != previous_sd.access_id)) {
        access_started_ms_ = last_tick_ms_; sd_seen_ = true;
    }
    if(data.storage.dataset_failure.revision!=data_.storage.dataset_failure.revision)
        dataset_notice_started_ms_=last_tick_ms_;
    const auto old_browse=data_.browse().revision;
    const auto old_dataset_revision=data_.datasets().revision;
    const auto old_export=data_.storage.export_job.phase;
    // V switches modes inside LogPoint without SetUiScreen. The old images
    // are no longer visible; reject late commits but retain any outstanding loan.
    if(data.dataset.generation!=data_.dataset.generation ||
       (showingMap(screen_,data_) && !showingMap(screen_,data)))screen_resources_.map.clear();
    data_.copyFrom(data,dataset_owned_ && screen_==ScreenId::Settings);updateMapCursor();
    // Activity is global; an older job snapshot cannot roll it back.
    if (newerRevision(previous_sd.revision, data_.storage.sd.revision)) data_.storage.sd = previous_sd;
    if (newerRevision(previous_rg.revision, data_.storage.rg.revision)) data_.storage.rg = previous_rg;
    for (auto& line : data_.place) line[sizeof(line) - 1] = '\0';
    if (data_.hasBrowse()) {
        auto& browse=data_.editBrowse();
        for(auto& name:browse.names) name[LogNameBytes-1]='\0';
        browse.name[LogNameBytes-1]='\0';
    }
    data_.storage.export_job.name[LogNameBytes-1]='\0';
    if(screen_==ScreenId::UsbStorage) {
        const auto phase=data_.storage.export_job.phase;
        if(phase!=old_export) {
            dialog_.close();
            switch(phase) {
                case ExportPhase::Idle:screen_resources_.usb.page=UsbPage::Menu;selection_=0;break;
                case ExportPhase::WaitEject:screen_resources_.usb.page=UsbPage::WaitEject;break;
                case ExportPhase::Selecting:screen_resources_.usb.page=UsbPage::Files;selection_=data_.browse().selected;break;
                case ExportPhase::Running:screen_resources_.usb.page=UsbPage::Progress;break;
                case ExportPhase::Complete:screen_resources_.usb.page=UsbPage::Complete;break;
                case ExportPhase::Failed:screen_resources_.usb.page=UsbPage::Error;break;
                default:screen_resources_.usb.page=UsbPage::Busy;break;
            }
        }
        if(phase==ExportPhase::Selecting && !data_.browse().loading && old_browse!=data_.browse().revision) {
            screen_resources_.usb.page=UsbPage::Files;selection_=data_.browse().selected;
        }
    }
    if(screen_==ScreenId::Logs && !data_.browse().loading && data_.browse().revision!=old_browse) {
        selection_=data_.browse().selected;
        dialog_.close(); // A refreshed list invalidates the previously shown target.
    }
    if(old_dataset_revision!=data_.datasets().revision && !data_.datasets().loading)datasetChanged();
    updateDialogState();
    data_.message[sizeof(data_.message) - 1] = '\0';
}

void UiModel::datasetChanged() {
    if (screen_ == ScreenId::Settings &&
        (screen_resources_.settings.page == SettingsPage::DatasetList || screen_resources_.settings.page == SettingsPage::DatasetDetail)) {
        if (data_.datasets().detail) {
            if (screen_resources_.settings.page != SettingsPage::DatasetDetail) { selection_ = 0; screen_resources_.settings.editing = false; }
            screen_resources_.settings.page = SettingsPage::DatasetDetail;
            const auto count = datasetDetailRows(data_.datasets(), data_.settings.language == UiLanguage::Japanese);
            if (selection_ >= count) selection_ = count-1;
        } else {
            screen_resources_.settings.page = SettingsPage::DatasetList; selection_ = data_.datasets().selected; screen_resources_.settings.editing = false;
            if (data_.datasets().count) screen_resources_.settings.dataset.list_start = data_.datasets().rows[0].number;
        }
    }
}

bool UiModel::canDeleteLog() const {
    return selection_ < data_.browse().count && !(data_.browse().active_mask & (1u << selection_));
}

void UiModel::updateDialogState() {
    if (dialog_.kind() == DialogKind::DeleteLog)
        dialog_.setActionEnabled(canDeleteLog());
    if (dialog_.kind() == DialogKind::EnterUsbMode) dialog_.setActionEnabled(screen_resources_.settings.usb_mode && usbEntryAllowed(data_));
    if (dialog_.kind() == DialogKind::ClearDataset || dialog_.kind() == DialogKind::RebuildIndex || dialog_.kind() == DialogKind::VerifyIndex)
        dialog_.setActionEnabled(data_.datasets().purpose==screen_resources_.settings.dataset.purpose && selection_<data_.datasets().count &&
            data_.datasets().rows[selection_].number==screen_resources_.settings.dataset.dialog_number && datasetRowBlock(data_,selection_)==DatasetError::None);
    if (dialog_.kind() == DialogKind::GpsControl) {
        if (gps_dialog_state_ != data_.recording.state) dialog_.close();
        else for (unsigned i = 0; i < dialog_.choices(); ++i)
            dialog_.setChoiceEnabled(i, gpsControlAllowed(data_, gpsControlAction(gps_dialog_state_, i)));
    }
}

void UiModel::acknowledge(uint32_t action_id, ActionResult result) {
    if (action_.kind != ActionKind::None && action_id == action_.action_id &&
        result != ActionResult::None) {
        if(result==ActionResult::Rejected && action_.kind==ActionKind::UsbExport)screen_resources_.usb.page=screen_resources_.usb.return_page;
        action_ = {};
    }
}

void UiModel::changed() {
    ++revision_;
    notification_pending_ = true;
    redraw_ = true;
}

unsigned UiModel::selectionCount() const {
    if (screen_ == ScreenId::Menu) return MenuCount;
    switch (screen_resources_.settings.page) {
        case SettingsPage::Root: return SettingsRootCount;
        case SettingsPage::Operation: return OperationRowCount;
        case SettingsPage::System: return SettingsSystemCount;
        case SettingsPage::Storage: return 4;
        case SettingsPage::Reset: return 1;
        case SettingsPage::DatasetMenu: return 3;
        case SettingsPage::DatasetList: return data_.datasets().count;
        case SettingsPage::DatasetDetail:
            return !data_.datasets().loading && data_.datasets().error==DatasetError::None ?
                datasetDetailRows(data_.datasets(),data_.settings.language==UiLanguage::Japanese) : 0;
        default: return 0;
    }
}

uint32_t UiModel::licenseTarget(UiKey key) const {
    if (key==UiKey::Up && screen_resources_.settings.license_offset) return screen_resources_.settings.license_offset-1;
    if (key==UiKey::Down && screen_resources_.settings.license_offset+SettingsLayout::LicenseRows<licenseLineCount()) return screen_resources_.settings.license_offset+1;
    if (key==UiKey::Right) {
        for (size_t n=0;n<licenseSectionCount();++n)
            if (licenseSectionStart(n)>screen_resources_.settings.license_offset) return licenseSectionStart(n);
    } else if (key==UiKey::Left && licenseSectionCount()) {
        size_t current=0;
        while(current+1<licenseSectionCount() && licenseSectionStart(current+1)<=screen_resources_.settings.license_offset)++current;
        return screen_resources_.settings.license_offset<licenseSectionStart(0)?0:licenseSectionStart(current?current-1:0);
    }
    return screen_resources_.settings.license_offset;
}

UiKeyGuide UiModel::keyGuide() const {
    UiKeyGuide guide;
    guide.accepting=action_.kind==ActionKind::None && !switchingMode(data_) &&
        !(isLogBrowser(screen_) && data_.browse().deleting);
    guide.root=!data_.boot.maintenance && isScreenRoot(screen_,screen_resources_.settings.page) && !screen_resources_.settings.editing && !screen_resources_.settings.card_info;
    guide.system=!data_.boot.maintenance;
    const bool dataset_page=screen_==ScreenId::Settings && (screen_resources_.settings.page==SettingsPage::DatasetMenu ||
        screen_resources_.settings.page==SettingsPage::DatasetList || screen_resources_.settings.page==SettingsPage::DatasetDetail || screen_resources_.settings.page==SettingsPage::DatasetIndex);
    const bool dataset_wait=dataset_page && data_.datasets().applying;
    guide.back=guide.accepting && !dataset_wait &&
        !(screen_==ScreenId::Settings && screen_resources_.settings.page==SettingsPage::DatasetIndex && data_.storage.busy);
    auto list=[&](unsigned count) {
        guide.vertical=true;guide.up=selection_>0;guide.down=selection_+1u<count;
    };
    if (screen_==ScreenId::Logs) {
        const auto& view=data_.browse();
        guide.vertical=true;guide.up=!view.loading && (selection_>0 || view.has_newer);
        guide.down=!view.loading && (selection_+1u<view.count || view.has_older);
        guide.remove=!view.loading && canDeleteLog();
    } else if (screen_==ScreenId::LogPoint) {
        guide.vertical=guide.horizontal=true;guide.up=guide.left=data_.browse().target>0;
        guide.down=guide.right=uint64_t(data_.browse().target)+1<data_.browse().summary.records;
    } else if (screen_==ScreenId::Menu) list(MenuCount);
    else if (screen_==ScreenId::Settings && !screen_resources_.settings.card_info) {
        if (screen_resources_.settings.editing) {
            guide.horizontal=true;
            guide.left=guide.right=!dataset_wait && (screen_resources_.settings.page==SettingsPage::System ||
                (screen_resources_.settings.page==SettingsPage::Operation && settingChoices(selection_)>1) ||
                (screen_resources_.settings.page==SettingsPage::DatasetDetail && !data_.datasets().loading && data_.datasets().error==DatasetError::None && data_.datasets().info.language_count>1));
        } else if (screen_resources_.settings.page==SettingsPage::Licenses) {
            guide.vertical=guide.horizontal=true;
            guide.up=licenseTarget(UiKey::Up)!=screen_resources_.settings.license_offset;guide.down=licenseTarget(UiKey::Down)!=screen_resources_.settings.license_offset;
            guide.left=licenseTarget(UiKey::Left)!=screen_resources_.settings.license_offset;guide.right=licenseTarget(UiKey::Right)!=screen_resources_.settings.license_offset;
        } else if (screen_resources_.settings.page==SettingsPage::DatasetList) {
            const auto& view=data_.datasets();guide.vertical=true;
            const bool ready=!view.loading && !view.applying && view.error==DatasetError::None;
            guide.up=ready && (selection_>0 || view.has_older);guide.down=ready && (selection_+1u<view.count || view.has_newer);
        } else if (screen_resources_.settings.page!=SettingsPage::About && screen_resources_.settings.page!=SettingsPage::Reset && screen_resources_.settings.page!=SettingsPage::DatasetIndex) {
            const bool ready=!dataset_wait && (screen_resources_.settings.page!=SettingsPage::DatasetDetail ||
                (!data_.datasets().loading && data_.datasets().error==DatasetError::None));
            list(ready?selectionCount():0);
            if (!ready) guide.up=guide.down=false;
        }
    }
    if(showingMap(screen_,data_)) {
        guide.zoom_in=map::supportedScale(data_.map.scale) && data_.map.scale>1;
        guide.zoom_out=map::supportedScale(data_.map.scale) && data_.map.scale<4;
    }
    if (!guide.accepting) guide.up=guide.down=guide.left=guide.right=guide.remove=guide.zoom_in=guide.zoom_out=false;
    return guide;
}

void UiModel::keyPressed(UiKey key, uint32_t now_ms) {
    last_input_ms_ = now_ms;
    // Physical Del remains Back/Cancel, but cannot select maintenance.
    const bool maintenance_key = key == UiKey::Escape;
    if (key == UiKey::Back) key = UiKey::Escape;
    if (generation_ == 0) {
        if (maintenance_key) early_maintenance_key_ = true;
        return;
    }
    if (action_.kind != ActionKind::None || switchingMode(data_)) return;
    if (screen_ == ScreenId::Boot) {
        if (maintenance_key) action(ActionKind::Maintenance, ScreenId::Settings);
        else if (!data_.boot.maintenance && data_.boot.complete && key == UiKey::Enter) action(ActionKind::ChangeScreen, ScreenId::LivePlace);
        return;
    }
    if (mode_ == DisplayMode::Background) { action(ActionKind::Wake); return; }
    if (dialog_.active()) {
        updateDialogState();
        if (!dialog_.active()) { changed(); return; }
        const auto kind = dialog_.kind();
        const auto choice = dialog_.choice();
        const auto result = dialog_.keyPressed(key);
        if(kind==DialogKind::LogShortcut && result==DialogInput::Accepted) {
            browseAction(choice==0?BrowseAction::Text:choice==1?BrowseAction::Map:BrowseAction::Statistics);return;
        }
        if (kind == DialogKind::GpsControl && result == DialogInput::Accepted) {
            const auto operation = gpsControlAction(gps_dialog_state_, choice);
            if (gps_dialog_state_ == data_.recording.state && gpsControlAllowed(data_, operation)) {
                action(ActionKind::Logging); action_.log_action = operation; action_.confirmed = true;
            } else changed();
            return;
        }
        if (result == DialogInput::Accepted) acceptDialog(kind);
        else if (result != DialogInput::Ignored) changed();
        return; // A modal owns all keys, including screen shortcuts.
    }
    if (screen_ == ScreenId::UsbStorage) {
        usbKey(key);
        return;
    }
    if (isLogBrowser(screen_) && data_.browse().deleting) return;
    if (!data_.boot.maintenance) {
        if (key == UiKey::ScreenOff) { action(ActionKind::Sleep); return; }
        if (key == UiKey::GpsControl) {
            if(viewingLogPoint(screen_) && data_.browse().playing) {
                data_.editBrowse().playing=false;browseAction(BrowseAction::PausePlayback);
            }
            openGpsControls(); return; }
    }
    if (!keyGuide().allowsKey(key)) return;
    if (!data_.boot.maintenance && isScreenRoot(screen_, screen_resources_.settings.page) && !screen_resources_.settings.editing && !screen_resources_.settings.card_info) {
        if (screen_ == ScreenId::Menu) {
            if (key == UiKey::Up) { --selection_; changed(); return; }
            if (key == UiKey::Down) { ++selection_; changed(); return; }
            if (key == UiKey::Enter) { action(ActionKind::ChangeScreen, MenuScreens[selection_].screen); return; }
            if (key == UiKey::Escape) { action(ActionKind::ChangeScreen, ScreenId::LivePlace); return; }
        }
        ScreenId target;
        // Del keeps Back semantics; physical Esc opens MENU.
        if (!(key == UiKey::Escape && !maintenance_key) && screenShortcut(key, target)) {
            if (target != screen_) action(ActionKind::ChangeScreen, target);
            return;
        }
    }
    if (screen_ == ScreenId::Menu) return;
    if(showingMap(screen_,data_) && (key==UiKey::ZoomIn || key==UiKey::ZoomOut)) {
        action(ActionKind::MapScale);
        action_.map_scale=key==UiKey::ZoomIn?data_.map.scale/2:data_.map.scale*2;
        return;
    }
    if (screen_ == ScreenId::Map) {
        if(key==UiKey::Escape)action(ActionKind::ChangeScreen);
        return;
    }
    if (screen_ == ScreenId::LivePlace) return;
    if (screen_ == ScreenId::GpsLogger) {
        gpsLoggerKey(key);
        return;
    }
    if(isLogBrowser(screen_)) {
        logBrowserKey(key);
        return;
    }
    settingsKey(key);
}

void UiModel::openGpsControls() {
    gps_dialog_state_ = data_.recording.state;
    dialog_.openActions(DialogKind::GpsControl, gpsControlChoices(data_), gpsControlDefault(gps_dialog_state_));
    updateDialogState();
    changed();
}

void UiModel::gpsLoggerKey(UiKey key) {
    if (key == UiKey::Escape) action(ActionKind::ChangeScreen, ScreenId::LivePlace);
    else if (key == UiKey::Enter) openGpsControls();
}

void UiModel::logBrowserKey(UiKey key) {
    const auto& view=data_.browse();
    if(key==UiKey::Escape) {
        action(ActionKind::ChangeScreen,screen_==ScreenId::Logs?ScreenId::LivePlace:
            screen_==ScreenId::LogProperties?ScreenId::Logs:screen_==ScreenId::LogStatistics?
            (view.statistics_from_point?ScreenId::LogPoint:ScreenId::LogProperties):ScreenId::LogProperties);
        return;
    }
    if(key==UiKey::LogMenu && (screen_==ScreenId::LogProperties || screen_==ScreenId::LogPoint) && !view.loading) {
        if(viewingLogPoint(screen_) && view.playing) {data_.editBrowse().playing=false;browseAction(BrowseAction::PausePlayback);}
        dialog_.openActions(DialogKind::LogShortcut,3,2);
        const bool valid=view.error==LogReadError::None;
        dialog_.setChoiceEnabled(0,valid && view.summary.records);
        dialog_.setChoiceEnabled(1,valid && logMapAvailable(data_) && view.summary.records);
        dialog_.setChoiceEnabled(2,valid);changed();return;
    }
    if(screen_==ScreenId::LogProperties) {
        if(key==UiKey::Enter && !view.loading && view.error==LogReadError::None && view.summary.records)browseAction(BrowseAction::OpenPoint);
        return;
    }
    if(screen_==ScreenId::LogStatistics) {
        if(key==UiKey::Recalculate && !view.loading)browseAction(BrowseAction::Recalculate);
        return;
    }
    if(screen_==ScreenId::LogPoint) {
        if(!view.summary.records)return;
        if(key==UiKey::Left) browseAction(BrowseAction::Point,view.target-1);
        if(key==UiKey::Right) browseAction(BrowseAction::Point,view.target+1);
        if(key==UiKey::Up) browseAction(BrowseAction::Point,view.target>10?view.target-10:0);
        if(key==UiKey::Down) browseAction(BrowseAction::Point,
            static_cast<uint32_t>(std::min(uint64_t(view.target)+10,uint64_t(view.summary.records-1))));
        if(key==UiKey::FirstPoint) browseAction(BrowseAction::Point,0);
        if(key==UiKey::LastPoint) browseAction(BrowseAction::Point,view.summary.records-1);
        if(key==UiKey::ToggleView && (!view.map_mode?logMapAvailable(data_):true)) browseAction(BrowseAction::ToggleView);
        if(key==UiKey::Playback && view.map_mode) browseAction(BrowseAction::Playback);
        return;
    }
    if(view.loading) return;
    if(screen_==ScreenId::Logs) {
        if(key==UiKey::DeleteLog && selection_<view.count) {
            dialog_.open(DialogKind::DeleteLog, canDeleteLog()); changed();
        } else if(key==UiKey::Up) {
            if(selection_) { --selection_; changed(); }
            else if(view.has_newer) browseAction(BrowseAction::Newer);
        } else if(key==UiKey::Down) {
            if(selection_+1<view.count) { ++selection_; changed(); }
            else if(view.has_older) browseAction(BrowseAction::Older);
        } else if(key==UiKey::Enter) {
            if(view.count) browseAction(BrowseAction::OpenFile,selection_);
            else browseAction(BrowseAction::Reload);
        }
    }
    return;
}

void UiModel::settingsKey(UiKey key) {
    if (screen_ == ScreenId::Settings && (screen_resources_.settings.page == SettingsPage::DatasetList || screen_resources_.settings.page == SettingsPage::DatasetDetail)) {
        const auto& view = data_.datasets();
        if (view.applying) return;
        if (key == UiKey::Escape) {
            if(dataset_borrow_.token)dataset_publishable_=false;
            if (screen_resources_.settings.editing) {
                screen_resources_.settings.editing = false; datasetAction(DatasetAction::CancelLanguage);
                std::strcpy(action_.settings.wordbook_language,screen_resources_.settings.dataset.edit_original);
            }
            else if (screen_resources_.settings.page == SettingsPage::DatasetDetail) {
                screen_resources_.settings.page = SettingsPage::DatasetList; selection_ = 0;
                datasetAction(DatasetAction::Open, screen_resources_.settings.dataset.list_start);
            } else { screen_resources_.settings.page = SettingsPage::DatasetMenu; selection_ = static_cast<uint8_t>(screen_resources_.settings.dataset.purpose); changed(); }
            return;
        }
        if (view.loading) return;
        if(screen_resources_.settings.page==SettingsPage::DatasetList && view.error!=DatasetError::None) {
            if(key==UiKey::Enter) datasetAction(DatasetAction::Open,screen_resources_.settings.dataset.list_start);
            return;
        }
        if (screen_resources_.settings.page == SettingsPage::DatasetList) {
            if (key == UiKey::Up) {
                if (selection_) { --selection_; changed(); }
                else if (view.has_older) datasetAction(DatasetAction::Older);
            } else if (key == UiKey::Down) {
                if (selection_+1 < view.count) { ++selection_; changed(); }
                else if (view.has_newer) datasetAction(DatasetAction::Newer);
            } else if (key == UiKey::Enter && view.count && datasetRowBlock(data_,selection_)==DatasetError::None) {
                screen_resources_.settings.dataset.dialog_number=view.rows[selection_].number;
                if(!screen_resources_.settings.dataset.dialog_number) {dialog_.open(DialogKind::ClearDataset);changed();}
                else if(screen_resources_.settings.dataset.purpose!=DatasetPurpose::Select) {
                    dialog_.open(screen_resources_.settings.dataset.purpose==DatasetPurpose::Rebuild?DialogKind::RebuildIndex:DialogKind::VerifyIndex);changed();
                } else datasetAction(DatasetAction::Detail,selection_);
            }
            return;
        }
        if (view.error != DatasetError::None) {
            if (key == UiKey::Enter && datasetRecoverable(view.error) && datasetSelectionBlock(data_,true)==DatasetError::None) datasetAction(DatasetAction::Apply);
            return;
        }
        const unsigned count = selectionCount();
        if (screen_resources_.settings.editing) {
            if (key == UiKey::Enter) { screen_resources_.settings.editing = false; changed(); }
            else if (key == UiKey::Left || key == UiKey::Right) datasetAction(DatasetAction::Language,key==UiKey::Right?1:0);
            return;
        }
        if (key == UiKey::Up) { --selection_; changed(); }
        if (key == UiKey::Down) { ++selection_; changed(); }
        if (key == UiKey::Enter) {
            if (selection_ == count-2) {
                std::strcpy(screen_resources_.settings.dataset.edit_original,view.info.language);screen_resources_.settings.editing = true; changed();
            }
            else if (selection_ == count-1 && datasetSelectionBlock(data_,true)==DatasetError::None) datasetAction(DatasetAction::Apply);
        }
        return;
    }
    if(screen_==ScreenId::Settings && screen_resources_.settings.page==SettingsPage::DatasetMenu) {
        if(data_.datasets().applying)return;
        if(key==UiKey::Escape) {screen_resources_.settings.page=SettingsPage::Operation;selection_=static_cast<uint8_t>(SettingId::Dataset);changed();return;}
        if(key==UiKey::Up) {--selection_;changed();}
        if(key==UiKey::Down) {++selection_;changed();}
        if(key==UiKey::Enter && datasetMenuBlock(data_,selection_)==DatasetError::None) {
            screen_resources_.settings.dataset.purpose=static_cast<DatasetPurpose>(selection_);
            screen_resources_.settings.dataset.list_start=screen_resources_.settings.dataset.purpose==DatasetPurpose::Select?0:1;
            screen_resources_.settings.page=SettingsPage::DatasetList;selection_=0;datasetAction(DatasetAction::Open,screen_resources_.settings.dataset.list_start);
        }
        return;
    }
    if(screen_==ScreenId::Settings && screen_resources_.settings.page==SettingsPage::DatasetIndex) {
        if(data_.datasets().applying)return;
        if(key==UiKey::Escape || key==UiKey::Enter) {
            if(data_.storage.busy)return;
            screen_resources_.settings.page=SettingsPage::DatasetList;selection_=0;datasetAction(DatasetAction::Open,screen_resources_.settings.dataset.list_start);
        }
        return;
    }
    if (screen_resources_.settings.card_info) {
        if (key == UiKey::Escape || key == UiKey::Enter) { screen_resources_.settings.card_info = false; changed(); }
        return;
    }
    if (screen_resources_.settings.page == SettingsPage::System) {
        if (screen_resources_.settings.editing) {
            if (key == UiKey::Escape) { screen_resources_.settings.editing = false; changed(); }
            else if (key == UiKey::Left || key == UiKey::Right) { screen_resources_.settings.edit_value ^= 1; changed(); }
            else if (key == UiKey::Enter) { screen_resources_.settings.usb_mode = screen_resources_.settings.edit_value != 0; screen_resources_.settings.editing = false; changed(); }
        } else if (key == UiKey::Escape) {
            screen_resources_.settings.usb_mode = false; screen_resources_.settings.page = SettingsPage::Root; selection_ = SettingsSystemRow; changed();
        } else if (key == UiKey::Up) { --selection_; changed(); }
        else if (key == UiKey::Down) { ++selection_; changed(); }
        else if (key == UiKey::Enter && !data_.storage.busy) {
            if (!selection_) { screen_resources_.settings.edit_value = screen_resources_.settings.usb_mode; screen_resources_.settings.editing = true; changed(); }
            else if (selection_ == 2 && screen_resources_.settings.usb_mode && usbEntryAllowed(data_)) {
                dialog_.open(DialogKind::EnterUsbMode); changed();
            } else if (selection_ == SettingsSystemResetRow) {
                screen_resources_.settings.page = SettingsPage::Reset; selection_ = 0; changed();
            }
        }
        return;
    }
    if (screen_resources_.settings.editing) {
        if (key == UiKey::Escape) { screen_resources_.settings.editing = false; changed(); }
        else if (key == UiKey::Left || key == UiKey::Right) {
            const unsigned count = settingChoices(selection_);
            screen_resources_.settings.edit_value = (screen_resources_.settings.edit_value + (key == UiKey::Right ? 1 : count - 1)) % count;
            changed();
        } else if (key == UiKey::Enter) {
            Settings value = data_.settings;
            if (setSettingValue(value, selection_, screen_resources_.settings.edit_value)) {
                action(ActionKind::SaveSettings);
                action_.settings = value;
                screen_resources_.settings.editing = false;
            }
        }
        return;
    }
    if (screen_resources_.settings.page == SettingsPage::About) {
        if (key == UiKey::Escape || key == UiKey::Enter) {
            screen_resources_.settings.page = SettingsPage::Root;
            selection_ = settingsRootIndex(SettingsRootItem::About); changed();
        }
        return;
    }
    if (key == UiKey::Escape) {
        if (screen_resources_.settings.page == SettingsPage::Root) {
            if (data_.boot.maintenance) { dialog_.open(DialogKind::RestartLogger); changed(); }
            else action(ActionKind::ChangeScreen, ScreenId::LivePlace);
        }
        else if (screen_resources_.settings.page == SettingsPage::Reset) { screen_resources_.settings.page = SettingsPage::System; selection_ = SettingsSystemResetRow; changed(); }
        else { screen_resources_.settings.page = SettingsPage::Root; selection_ = 0; changed(); }
        return;
    }
    if (screen_resources_.settings.page == SettingsPage::Licenses) {
        const auto target = licenseTarget(key);
        if (target != screen_resources_.settings.license_offset) { screen_resources_.settings.license_offset = target; changed(); }
        return;
    }
    if (key == UiKey::Up) { --selection_; changed(); }
    if (key == UiKey::Down) { ++selection_; changed(); }
    if (key != UiKey::Enter) return;
    if (screen_resources_.settings.page == SettingsPage::Root) {
        switch (static_cast<SettingsRootItem>(selection_)) {
            case SettingsRootItem::Operation: screen_resources_.settings.page = SettingsPage::Operation; break;
            case SettingsRootItem::System: screen_resources_.settings.page = SettingsPage::System; screen_resources_.settings.usb_mode = false; break;
            case SettingsRootItem::Storage: screen_resources_.settings.page = SettingsPage::Storage; break;
            case SettingsRootItem::Licenses: screen_resources_.settings.page = SettingsPage::Licenses; break;
            case SettingsRootItem::About: screen_resources_.settings.page = SettingsPage::About; break;
            default: return;
        }
        selection_ = 0; screen_resources_.settings.license_offset = 0;
        changed();
    } else if (screen_resources_.settings.page == SettingsPage::Operation && selection_ == static_cast<unsigned>(SettingId::Dataset)) {
        screen_resources_.settings.page = SettingsPage::DatasetMenu; selection_ = 0;changed();
    } else if (screen_resources_.settings.page == SettingsPage::Operation && settingEditable(selection_) && !data_.storage.busy) {
        screen_resources_.settings.edit_value = settingValue(data_.settings, selection_);
        screen_resources_.settings.editing = true; changed();
    } else if (screen_resources_.settings.page == SettingsPage::Reset && !data_.storage.busy) {
        dialog_.open(DialogKind::ResetSettings); changed();
    } else if (screen_resources_.settings.page == SettingsPage::Storage && !data_.storage.busy) {
        if (selection_ == 0) { screen_resources_.settings.card_info = true; changed(); }
        else if (selection_ == 3) { dialog_.open(DialogKind::FormatSd); changed(); }
        else if (selection_ == 1 || selection_ == 2) {
            action(ActionKind::Storage);
            action_.storage_operation = selection_ == 1 ? StorageOperation::Remount : StorageOperation::Unmount;
        }
    }
}

void UiModel::usbAction(UsbAction kind) {
    screen_resources_.usb.return_page=screen_resources_.usb.page;
    action(ActionKind::UsbExport);action_.usb_action=kind;
    if(kind==UsbAction::Start) {
        action_.browse_index=screen_resources_.usb.file;action_.browse_revision=data_.browse().revision;
        action_.export_format=static_cast<ExportFormat>(selection_);
    }
    if(kind!=UsbAction::Older && kind!=UsbAction::Newer)screen_resources_.usb.page=UsbPage::Busy;
}
void UiModel::usbKey(UiKey key) {
    if(screen_resources_.usb.page==UsbPage::Menu) {
        if(key==UiKey::Up && selection_) {selection_=0;changed();}
        else if(key==UiKey::Down && !selection_) {selection_=1;changed();}
        else if(key==UiKey::Escape || (key==UiKey::Enter && selection_)) {
            dialog_.open(DialogKind::ExitUsbMode);changed();
        } else if(key==UiKey::Enter && data_.storage.usb.ready)usbAction(UsbAction::Begin);
    } else if(screen_resources_.usb.page==UsbPage::WaitEject) {
        if(key==UiKey::Escape || key==UiKey::Enter)usbAction(UsbAction::Publish);
    } else if(screen_resources_.usb.page==UsbPage::Files) {
        const auto& view=data_.browse();
        if(key==UiKey::Escape) {usbAction(UsbAction::Publish);return;}
        if(view.loading)return;
        if(key==UiKey::Up) {
            if(selection_) {--selection_;changed();}
            else if(view.has_newer)usbAction(UsbAction::Newer);
        } else if(key==UiKey::Down) {
            if(selection_+1u<view.count) {++selection_;changed();}
            else if(view.has_older)usbAction(UsbAction::Older);
        } else if(key==UiKey::Enter && selection_<view.count && view.error==LogReadError::None) {
            screen_resources_.usb.file=selection_;selection_=0;screen_resources_.usb.page=UsbPage::Format;changed();
        }
    } else if(screen_resources_.usb.page==UsbPage::Format) {
        if(key==UiKey::Escape) {screen_resources_.usb.page=UsbPage::Files;selection_=screen_resources_.usb.file;changed();}
        else if(key==UiKey::Up && selection_) {--selection_;changed();}
        else if(key==UiKey::Down && selection_<2) {++selection_;changed();}
        else if(key==UiKey::Enter)usbAction(UsbAction::Start);
    } else if(screen_resources_.usb.page==UsbPage::Complete || screen_resources_.usb.page==UsbPage::Error) {
        if(key==UiKey::Enter || key==UiKey::Escape)usbAction(UsbAction::Publish);
    }
}

void UiModel::acceptDialog(DialogKind kind) {
    switch (kind) {
        case DialogKind::RestartLogger:
            action(ActionKind::RestartLogger); action_.confirmed = true; break;
        case DialogKind::DeleteLog:
            browseAction(BrowseAction::Delete, selection_);
            break;
        case DialogKind::FormatSd:
            action(ActionKind::Storage); action_.storage_operation = StorageOperation::Format;
            break;
        case DialogKind::ResetSettings:
            action(ActionKind::ResetSettings); action_.settings = defaultSettings();
            break;
        case DialogKind::EnterUsbMode: case DialogKind::ExitUsbMode:
            action(ActionKind::Storage);
            action_.storage_operation = kind == DialogKind::EnterUsbMode ? StorageOperation::EnterUsbMode : StorageOperation::ExitUsbMode;
            break;
        case DialogKind::ClearDataset:
            datasetAction(DatasetAction::Clear);break;
        case DialogKind::RebuildIndex:case DialogKind::VerifyIndex:
            datasetAction(kind==DialogKind::RebuildIndex?DatasetAction::RebuildIndex:DatasetAction::VerifyIndex,selection_);
            screen_resources_.settings.page=SettingsPage::DatasetIndex;changed();break;
        case DialogKind::LogShortcut: case DialogKind::GpsControl: case DialogKind::None: return;
    }
    action_.confirmed = true;
}

void UiModel::datasetAction(DatasetAction kind,uint32_t index) {
    action(ActionKind::Dataset);action_.dataset_action=kind;action_.browse_index=index;
    action_.dataset_purpose=screen_resources_.settings.dataset.purpose;action_.browse_revision=data_.datasets().revision;
    const auto& view=data_.datasets();
    switch(kind) {
        case DatasetAction::Open:action_.dataset_number=static_cast<uint8_t>(index);break;
        case DatasetAction::Older:case DatasetAction::Newer:
            if(view.count)action_.dataset_number=view.rows[kind==DatasetAction::Older?0:view.count-1].number;
            break;
        case DatasetAction::Detail:case DatasetAction::RebuildIndex:case DatasetAction::VerifyIndex:
            if(index<view.count)action_.dataset_number=view.rows[index].number;
            break;
        case DatasetAction::Clear:break;
        default:action_.dataset_number=view.number;break;
    }
    if(kind==DatasetAction::Language || kind==DatasetAction::Apply)
        std::snprintf(action_.settings.wordbook_language,sizeof(action_.settings.wordbook_language),"%s",view.info.language);
    else if(kind==DatasetAction::Detail && action_.dataset_number==data_.settings.dataset)
        std::memcpy(action_.settings.wordbook_language,data_.settings.wordbook_language,sizeof(action_.settings.wordbook_language));
    if(dataset_borrow_.token)dataset_publishable_=false;
}

void UiModel::browseAction(BrowseAction kind,uint32_t index) {
    action(ActionKind::Browse);
    action_.browse_action=kind; action_.browse_index=index; action_.browse_revision=data_.browse().revision;
}

void UiModel::action(ActionKind kind, ScreenId target) {
    if (++next_action_id_ == 0) ++next_action_id_;
    action_ = {};
    action_.kind = kind; action_.action_id = next_action_id_;
    action_.screen_generation = generation_; action_.target = target;
    changed();
}

void UiModel::tick(uint32_t now_ms) {
    const auto previous_second = clock_.secondsJst();
    clock_.tick(now_ms);
    if (screen_ == ScreenId::LivePlace && mode_ == DisplayMode::View &&
        clock_.secondsJst() != previous_second) redraw_ = true;
    const bool was_lit = sdAccessLit();
    const bool was_dataset_notice=datasetFailureVisible();
    last_tick_ms_ = now_ms;
    // Start the visible interval when Live Place can actually show the notice.
    if(screen_!=ScreenId::LivePlace || mode_==DisplayMode::Background) dataset_notice_started_ms_=now_ms;
    if(was_dataset_notice!=datasetFailureVisible())redraw_=true;
    if (sdAccessLit() != was_lit) redraw_ = true;
    // Keep a requested scan visible; the inactivity interval starts at completion.
    // Explicit S still enters Background through the normal key handler.
    if(screen_==ScreenId::LogStatistics && data_.browse().loading) {last_input_ms_=now_ms;return;}
    if (screen_ == ScreenId::Boot || screen_ == ScreenId::UsbStorage || switchingMode(data_) || data_.datasets().applying ||
        (screen_==ScreenId::Logs && data_.browse().deleting)) return;
    if (data_.message[0] && now_ms - data_.message_at_ms >= 3000u) {
        data_.message[0] = '\0';
        redraw_ = true;  // Local toast expiry does not notify App.
    }
    if (!last_input_ms_) last_input_ms_ = now_ms;
    if (data_.boot.maintenance || mode_ != DisplayMode::View || generation_ == 0 || action_.kind != ActionKind::None ||
        !validSettings(data_.settings)) return;
    const auto timeout = ScreenTimeouts[data_.settings.screen_timeout];
    if (timeout && now_ms - last_input_ms_ >= timeout * 1000u) {
        screen_resources_.settings.editing = false; dialog_.close();
        action(ActionKind::Sleep);
    }
}

UiSnapshot UiModel::snapshot() const {
    return {screen_, generation_, revision_, last_input_ms_, selection_, action_, screen_resources_.settings.page};
}

bool UiModel::takeRedraw() {
    const bool value = redraw_;
    redraw_ = false;
    return value;
}

}  // namespace geoputer

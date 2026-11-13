#include "GpsUiModel.h"
#include "../../LogReplay.h"
#include "AlarmText.h"
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
                                 data.storage.operation == StorageOperation::EnterWifiMode);
}

OperationStatus GpsUiModel::setScreen(const SetUiScreen& command) {
    if (command.screen != ScreenId::LivePlace && command.screen != ScreenId::Settings && command.screen != ScreenId::Boot && command.screen != ScreenId::Menu && command.screen != ScreenId::Map && command.screen != ScreenId::GpsLogger && !isLogBrowser(command.screen)) {
        return OperationStatus::InvalidRequest;
    }
    if (command.generation <= generation_) return OperationStatus::StaleContent;
    // Switching screens cannot silently erase an unprocessed confirmed action.
    if (action_.kind != ActionKind::None && (command.processed_action_id != action_.action_id || command.action_result == ActionResult::None)) {
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
    updateScreenName();
    return OperationStatus::Ok;
}

OperationStatus GpsUiModel::updateContent(const UpdateUiContent& command) {
    if (command.generation != generation_ || command.data.revision < data_.revision) {
        return OperationStatus::StaleContent;
    }
    copyData(command.data);
    mode_ = command.mode;
    acknowledge(command.processed_action_id, command.action_result);
    redraw_ = true;
    updateScreenName();
    return OperationStatus::Ok;
}

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

void GpsUiModel::copyData(const UiData& data) {
    if (data.gps.fix.valid) clock_.setInitialUtc(data.gps.fix.utc_ms, last_tick_ms_);
    const auto previous_sd = data_.storage.sd;
    const auto previous_rg = data_.storage.rg;
    if (newerRevision(data.storage.sd.revision, previous_sd.revision) && (!sd_seen_ || data.storage.sd.access_id != previous_sd.access_id)) {
        access_started_ms_ = last_tick_ms_; sd_seen_ = true;
    }
    if(data.storage.dataset_failure.revision!=data_.storage.dataset_failure.revision)
        dataset_notice_started_ms_=last_tick_ms_;
    const auto old_browse=data_.browse().revision;
    const auto old_dataset_revision=data_.datasets().revision;

    // V switches modes inside LogPoint without SetUiScreen. The old images
    // are no longer visible; reject late commits but retain any outstanding loan.
    if(data.dataset.generation!=data_.dataset.generation || (showingMap(screen_,data_) && !showingMap(screen_,data)))screen_resources_.map.clear();
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

    if(screen_==ScreenId::Logs && !data_.browse().loading && data_.browse().revision!=old_browse) {
        selection_=data_.browse().selected;
        dialog_.close(); // A refreshed list invalidates the previously shown target.
    }
    if(old_dataset_revision!=data_.datasets().revision && !data_.datasets().loading)datasetChanged();
    updateDialogState();
    data_.message[sizeof(data_.message) - 1] = '\0';
}

void GpsUiModel::datasetChanged() {
    if (screen_ == ScreenId::Settings && (screen_resources_.settings.page == SettingsPage::DatasetList || screen_resources_.settings.page == SettingsPage::DatasetDetail)) {
        if (data_.datasets().detail) {
            if (screen_resources_.settings.page != SettingsPage::DatasetDetail) { selection_ = 0; screen_resources_.settings.editing = false; }
            screen_resources_.settings.page = SettingsPage::DatasetDetail;
            const auto count = datasetDetailRows(data_.datasets(), data_.settings.language == UiLanguage::Japanese);
            if (selection_ >= count) selection_ = count-1;
        } else {
            screen_resources_.settings.page = SettingsPage::DatasetList; selection_ = data_.datasets().selected; screen_resources_.settings.editing = false;
            if (data_.datasets().count) {
                screen_resources_.settings.dataset.list_start = data_.datasets().rows[0].id;
                screen_resources_.settings.dataset.page=data_.datasets().page;
            }
        }
    }
}

bool GpsUiModel::canDeleteLog() const {
    return selection_ < data_.browse().count && !(data_.browse().active_mask & (1u << selection_));
}

void GpsUiModel::updateDialogState() {
    if (dialog_.kind() == DialogKind::DeleteLog)
        dialog_.setActionEnabled(canDeleteLog());
    if (dialog_.kind() == DialogKind::EnterUsbMode || dialog_.kind()==DialogKind::EnterWifiMode) dialog_.setActionEnabled(screen_resources_.settings.usb_mode && usbEntryAllowed(data_));
    if (dialog_.kind() == DialogKind::ClearDataset || dialog_.kind() == DialogKind::RebuildIndex || dialog_.kind() == DialogKind::VerifyIndex)
        dialog_.setActionEnabled(data_.datasets().purpose==screen_resources_.settings.dataset.purpose && selection_<data_.datasets().count &&
            data_.datasets().rows[selection_].id==screen_resources_.settings.dataset.dialog_id && datasetRowBlock(data_,selection_)==DatasetError::None);
    if (dialog_.kind() == DialogKind::GpsControl) {
        if (gps_dialog_state_ != data_.recording.state) dialog_.close();
        else for (unsigned i = 0; i < dialog_.choices(); ++i)
            dialog_.setChoiceEnabled(i, gpsControlAllowed(data_, gpsControlAction(gps_dialog_state_, i)));
    }
}

void GpsUiModel::acknowledge(uint32_t action_id, ActionResult result) {
    if (action_.kind != ActionKind::None && action_id == action_.action_id && result != ActionResult::None) {
        action_ = {};
    }
}

void GpsUiModel::changed() {
    updateScreenName();
    ++revision_;
    notification_pending_ = true;
    redraw_ = true;
}

unsigned GpsUiModel::selectionCount() const {
    if (screen_ == ScreenId::Menu) return MenuCount;
    switch (screen_resources_.settings.page) {
        case SettingsPage::Root: return SettingsRootCount;
        case SettingsPage::Operation: return OperationRowCount;
        case SettingsPage::System: return SettingsSystemCount;
        case SettingsPage::Storage: return 4;
        case SettingsPage::Reset: return 1;
        case SettingsPage::Alarm:return AlarmRows;
        case SettingsPage::DatasetMenu: return 3;
        case SettingsPage::DatasetList: return data_.datasets().count;
        case SettingsPage::DatasetDetail:
            return !data_.datasets().loading && data_.datasets().error==DatasetError::None ?
                datasetDetailRows(data_.datasets(),data_.settings.language==UiLanguage::Japanese) : 0;
        default: return 0;
    }
}

uint32_t GpsUiModel::licenseTarget(UiKey key) const {
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

UiKeyGuide GpsUiModel::keyGuide() const {
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
        const auto& view=data_.browse();guide.vertical=guide.horizontal=true;
        if(view.map_mode && view.playing) {
            guide.up=replay::speed(view)+1<replay::SpeedCount;guide.down=replay::speed(view)>0;
            guide.left=!view.replay_reverse;guide.right=view.replay_reverse;
        } else {
            guide.up=guide.left=view.summary.records && view.target>0;
            guide.down=guide.right=uint64_t(view.target)+1<view.summary.records;
        }
    } else if (screen_==ScreenId::Menu) list(MenuCount);
    else if (screen_==ScreenId::Settings && !screen_resources_.settings.card_info) {
        if (screen_resources_.settings.page==SettingsPage::Licenses) {
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
    if(screen_==ScreenId::Settings && !screen_resources_.settings.card_info && screen_resources_.settings.page!=SettingsPage::Licenses) {
        guide.horizontal=editableSettingChoices()>1;
        guide.left=guide.right=guide.horizontal;
        if(editDirty() && data_.storage.busy && screen_resources_.settings.page!=SettingsPage::System)
            guide.up=guide.down=false;
    }
    if(showingMap(screen_,data_)) {
        guide.zoom_in=map::supportedScale(data_.map.scale) && data_.map.scale>1;
        guide.zoom_out=map::supportedScale(data_.map.scale) && data_.map.scale<4;
    }
    if (!guide.accepting) guide.up=guide.down=guide.left=guide.right=guide.remove=guide.zoom_in=guide.zoom_out=false;
    return guide;
}

// Use the same numeric destinations as direct presses; skip the current view
// and unavailable destinations. Other means that cycling has no destination.
UiKey GpsUiModel::nextNumericKey() const {
    const auto guide=keyGuide();
    if(mode_!=DisplayMode::View || dialog_.active() || !guide.accepting || !guide.system)return UiKey::Other;
    if(guide.root) {
        for(unsigned i=0;i<MenuCount;++i)if(MenuScreens[i].screen==screen_)return MenuScreens[(i+1)%MenuCount].key;
        return MenuScreens[0].key; // MENU itself is not a numeric destination.
    }
    if(isLogViewScreen(screen_)) {
        constexpr unsigned count=sizeof(LogViewShortcuts)/sizeof(*LogViewShortcuts);
        unsigned start=0;
        for(unsigned i=0;i<count;++i)if(selectedLogView(screen_,data_,LogViewShortcuts[i].action))start=(i+1)%count;
        for(unsigned n=0;n<count;++n) {
            const auto& item=LogViewShortcuts[(start+n)%count];
            if(!selectedLogView(screen_,data_,item.action) && logViewAvailable(screen_,data_,item.action))return item.key;
        }
    }
    return UiKey::Other;
}

// Only advertise Enter where it selects, confirms, retries or returns.
// Keep disabled choices visible in the same way as other local operations.
void GpsUiModel::appendEnterShortcut(UiShortcutGuide& row,bool accepting) const {
    bool enabled=true;
    if(dialog_.active()) enabled=dialog_.choiceEnabled(dialog_.choice());
    else if(screen_==ScreenId::Boot) {
        if(data_.boot.maintenance || !data_.boot.complete)return;
    } else if(screen_==ScreenId::Logs) enabled=!data_.browse().loading;
    else if(screen_==ScreenId::LogProperties) {
        const auto& view=data_.browse();
        enabled=!view.loading && view.error==LogReadError::None && view.summary.records;
    } else if(screen_==ScreenId::Settings) {
        const auto& settings=screen_resources_.settings;
        if(!settings.card_info) switch(settings.page) {
            case SettingsPage::Licenses:return;
            case SettingsPage::Alarm:if(selection_<AlarmReloadRow && !editDirty())return;enabled=!data_.storage.busy && !data_.storage.nvs_error && (selection_==AlarmReloadRow?logMapAvailable(data_):data_.storage.alarm.usable());break;
            case SettingsPage::DatasetMenu:enabled=!data_.datasets().applying && datasetMenuBlock(data_,selection_)==DatasetError::None;break;
            case SettingsPage::DatasetList: {
                const auto& view=data_.datasets();
                enabled=!view.loading && !view.applying && (view.error!=DatasetError::None ||
                    (selection_<view.count && datasetRowBlock(data_,selection_)==DatasetError::None));break;
            }
            case SettingsPage::DatasetDetail: {
                const auto& view=data_.datasets();
                if(view.loading || view.applying)enabled=false;
                else if(view.error!=DatasetError::None) {
                    if(!datasetRecoverable(view.error))return;
                    enabled=datasetSelectionBlock(data_,true)==DatasetError::None;
                } else {
                    const auto count=selectionCount();
                    if(count<2 || selection_<count-2 || (selection_==count-2 && !editDirty()))return;
                    if(selection_==count-1)enabled=datasetSelectionBlock(data_,true)==DatasetError::None;
                }
                break;
            }
            case SettingsPage::DatasetIndex:enabled=!data_.datasets().applying && !data_.storage.busy;break;
            case SettingsPage::System:
                if(selection_==0 && !editDirty())return;
                if(selection_!=0 && selection_!=2 && selection_!=SettingsSystemResetRow)return;
                enabled=!data_.storage.busy && (selection_!=2 || (settings.usb_mode && usbEntryAllowed(data_)));
                break;
            case SettingsPage::Operation:
                if(selection_!=static_cast<unsigned>(SettingId::Dataset) && !editDirty())return;
                enabled=!data_.storage.busy;
                break;
            case SettingsPage::Storage:case SettingsPage::Reset:enabled=!data_.storage.busy;break;
            default:break;
        }
    } else if(screen_!=ScreenId::GpsLogger && screen_!=ScreenId::Menu)return;
    row.key(UiKey::Enter,accepting && enabled);
}

UiShortcutGuide GpsUiModel::shortcutGuide() const {
    UiShortcutGuide row;
    if(mode_==DisplayMode::Background)return row;
    const auto guide=keyGuide();
    const auto active=guide.accepting;
    if(audio_active_ && !audio_preview_) {row.key(UiKey::Escape,active);return row;}
    if(dialog_.active()) {
        row.key(UiKey::Escape,active);row.group();appendEnterShortcut(row,active);
        bool left=false,right=false;
        for(unsigned i=0;i<dialog_.choices();++i)if(dialog_.choiceEnabled(i)) {
            left|=i<dialog_.choice();right|=i>dialog_.choice();
        }
        row.key(UiKey::Left,active && left);row.key(UiKey::Right,active && right);return row;
    }
    if(screen_==ScreenId::Boot) {row.key(UiKey::Escape,active);row.group();appendEnterShortcut(row,active);return row;}


    // Global shortcuts always precede numeric destinations and local operations.
    row.key(UiKey::Escape,guide.back);
    if(guide.system) {row.key(UiKey::ScreenOff,active);row.key(UiKey::GpsControl,active);}
    if(guide.root) {
        row.group();for(const auto& item:MenuScreens)row.key(item.key,active,item.screen==screen_);
    } else if(isLogViewScreen(screen_)) {
        row.group();for(const auto& item:LogViewShortcuts)
            row.key(item.key,active && logViewAvailable(screen_,data_,item.action),selectedLogView(screen_,data_,item.action));
    }
    row.group();
    appendEnterShortcut(row,active);
    const auto& view=data_.browse();
    if(screen_==ScreenId::LogPoint) {
        const bool points=view.summary.records!=0;
        row.key(UiKey::FirstPoint,active && points && view.target>0);
        row.key(UiKey::LastPoint,active && uint64_t(view.target)+1<view.summary.records);
        if(view.map_mode) {
            row.key(UiKey::ZoomIn,guide.zoom_in);row.key(UiKey::ZoomOut,guide.zoom_out);
            row.key(UiKey::Playback,active && points && view.error==LogReadError::None);
        }
    } else if(screen_==ScreenId::LogStatistics) {
        row.key(UiKey::Recalculate,active && !view.loading);
    } else if(screen_==ScreenId::Logs)row.key(UiKey::DeleteLog,guide.remove);
    else if(screen_==ScreenId::Map) {row.key(UiKey::ZoomIn,guide.zoom_in);row.key(UiKey::ZoomOut,guide.zoom_out);}
    if(guide.horizontal) {row.key(UiKey::Left,guide.left);row.key(UiKey::Right,guide.right);}
    if(guide.vertical) {row.key(UiKey::Up,guide.up);row.key(UiKey::Down,guide.down);}
    return row;
}

void GpsUiModel::keyPressed(UiKey key, uint32_t now_ms) {
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
    if(audio_active_ && !audio_preview_) {action(ActionKind::SilenceAlarm);return;}
    if (mode_ == DisplayMode::Background) { action(ActionKind::Wake); return; }
    if (dialog_.active()) {
        updateDialogState();
        if (!dialog_.active()) { changed(); return; }
        const auto kind = dialog_.kind();
        const auto choice = dialog_.choice();
        const auto result = dialog_.keyPressed(key);
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


    if (isLogBrowser(screen_) && data_.browse().deleting) return;
    if (!data_.boot.maintenance) {
        if (key == UiKey::ScreenOff) { action(ActionKind::Sleep); return; }
        if (key == UiKey::GpsControl) {
            if(viewingLogPoint(screen_) && data_.browse().playing) {
                data_.editBrowse().playing=false;browseAction(BrowseAction::PausePlayback);
            }
            openGpsControls(); return; }
    }
    if(key==UiKey::Tab) {key=nextNumericKey();if(key==UiKey::Other)return;}
    if (!keyGuide().allowsKey(key)) return;
    if (!data_.boot.maintenance && isScreenRoot(screen_, screen_resources_.settings.page) && !screen_resources_.settings.editing && !screen_resources_.settings.card_info) {
        if (screen_ == ScreenId::Menu) {
            if (key == UiKey::Up) { --selection_; changed(); return; }
            if (key == UiKey::Down) { ++selection_; changed(); return; }
            if (key == UiKey::Enter) { action(ActionKind::ChangeScreen, MenuScreens[selection_].screen); return; }
        }
        if(key==UiKey::Escape) {
            // Only LIVE opens the selector. Del has no Back destination on LIVE.
            if(screen_!=ScreenId::LivePlace || maintenance_key)
                action(ActionKind::ChangeScreen,screen_==ScreenId::LivePlace?ScreenId::Menu:ScreenId::LivePlace);
            return;
        }
        ScreenId target;
        if(screenShortcut(key,target)) {
            if(target!=screen_)action(ActionKind::ChangeScreen,target);
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

void GpsUiModel::openGpsControls() {
    gps_dialog_state_ = data_.recording.state;
    dialog_.openActions(DialogKind::GpsControl, gpsControlChoices(data_), gpsControlDefault(gps_dialog_state_));
    updateDialogState();
    changed();
}

void GpsUiModel::gpsLoggerKey(UiKey key) {
    if (key == UiKey::Escape) action(ActionKind::ChangeScreen, ScreenId::LivePlace);
    else if (key == UiKey::Enter) openGpsControls();
}

void GpsUiModel::logBrowserKey(UiKey key) {
    const auto& view=data_.browse();
    if(key==UiKey::Escape) {
        action(ActionKind::ChangeScreen,screen_==ScreenId::Logs?ScreenId::LivePlace:
            screen_==ScreenId::LogProperties?ScreenId::Logs:screen_==ScreenId::LogStatistics?
            (view.statistics_from_point?ScreenId::LogPoint:ScreenId::LogProperties):ScreenId::LogProperties);
        return;
    }
    if(isLogViewScreen(screen_))for(const auto& item:LogViewShortcuts)if(key==item.key) {
        if(!selectedLogView(screen_,data_,item.action) && logViewAvailable(screen_,data_,item.action))browseAction(item.action);
        return;
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
        if(view.map_mode && view.playing) {
            switch(key) {
                case UiKey::Up:browseAction(BrowseAction::ReplayFaster);return;
                case UiKey::Down:browseAction(BrowseAction::ReplaySlower);return;
                case UiKey::Left:browseAction(BrowseAction::ReplayReverse);return;
                case UiKey::Right:browseAction(BrowseAction::ReplayForward);return;
                default:break;
            }
        }
        if(key==UiKey::Left) browseAction(BrowseAction::Point,view.target-1);
        if(key==UiKey::Right) browseAction(BrowseAction::Point,view.target+1);
        if(key==UiKey::Up) browseAction(BrowseAction::Point,view.target>10?view.target-10:0);
        if(key==UiKey::Down) browseAction(BrowseAction::Point,
            static_cast<uint32_t>(std::min(uint64_t(view.target)+10,uint64_t(view.summary.records-1))));
        if(key==UiKey::FirstPoint) browseAction(BrowseAction::Point,0);
        if(key==UiKey::LastPoint) browseAction(BrowseAction::Point,view.summary.records-1);
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

unsigned GpsUiModel::editableSettingChoices() const {
    if(screen_!=ScreenId::Settings || screen_resources_.settings.card_info)return 0;
    const auto& settings=screen_resources_.settings;
    switch(settings.page) {
        case SettingsPage::Operation:
            return !data_.storage.busy && settingEditable(selection_)?settingChoices(selection_):0;
        case SettingsPage::System:return !data_.storage.busy && selection_==0?3:0;
        case SettingsPage::Alarm:
            return !data_.storage.busy && !data_.storage.nvs_error && data_.storage.alarm.usable()?
                alarmChoices(selection_):0;
        case SettingsPage::DatasetDetail: {
            const auto& view=data_.datasets();const auto count=selectionCount();
            return !view.loading && !view.applying && view.error==DatasetError::None && count>=2 && selection_==count-2?
                view.info.language_count:0;
        }
        default:return 0;
    }
}

bool GpsUiModel::editDirty() const {
    const auto& settings=screen_resources_.settings;
    if(!settings.editing)return false;
    if(settings.page==SettingsPage::DatasetDetail)
        return data_.datasets().loading || std::strcmp(settings.dataset.edit_original,data_.datasets().info.language)!=0;
    return settings.edit_value!=settings.edit_original;
}

bool GpsUiModel::commitSettingEdit() {
    auto& settings=screen_resources_.settings;
    if(!settings.editing)return true;
    if(editDirty()) {
        if(settings.page==SettingsPage::Operation) {
            if(data_.storage.busy)return false;
            Settings value=data_.settings;
            if(!setSettingValue(value,selection_,settings.edit_value))return false;
            if(!sameSettings(value,data_.settings)) {
                action(ActionKind::SaveSettings);action_.settings=value;
            }
        } else if(settings.page==SettingsPage::Alarm) {
            if(data_.storage.busy || data_.storage.nvs_error || !data_.storage.alarm.usable())return false;
            if(settings.edit_value!=alarmValue(data_.storage.alarm.settings,selection_)) {
                action(ActionKind::SaveAlarm);action_.alarm_settings=data_.storage.alarm.settings;
                setAlarmValue(action_.alarm_settings,selection_,settings.edit_value);
                action_.alarm_identity=data_.storage.alarm.identity;
            }
        } else if(settings.page==SettingsPage::System)settings.usb_mode=settings.edit_value;
        // Dataset language is a local candidate. Apply still owns persistence/restart.
    }
    settings.editing=false;changed();return true;
}

void GpsUiModel::settingsKey(UiKey key) {
    auto& edit=screen_resources_.settings;
    if(edit.editing) {
        if(key==UiKey::Escape) {
            const bool dirty=editDirty();edit.editing=false;
            if(dirty) {
                if(edit.page==SettingsPage::DatasetDetail) {
                    datasetAction(DatasetAction::CancelLanguage);
                    std::strcpy(action_.settings.wordbook_language,edit.dataset.edit_original);
                }
                changed();return; // A second Escape leaves the page.
            }
        } else if(key==UiKey::Enter || key==UiKey::Up || key==UiKey::Down) {
            if(!commitSettingEdit())return;
            if(key==UiKey::Enter)return;
        }
    }
    if(key==UiKey::Left || key==UiKey::Right) {
        const auto count=editableSettingChoices();
        if(count>1) {
            if(!edit.editing) {
                if(edit.page==SettingsPage::DatasetDetail)
                    std::strcpy(edit.dataset.edit_original,data_.datasets().info.language);
                else {
                    edit.edit_original=edit.page==SettingsPage::System?edit.usb_mode:
                        edit.page==SettingsPage::Alarm?alarmValue(data_.storage.alarm.settings,selection_):settingValue(data_.settings,selection_);
                    edit.edit_value=edit.edit_original;
                }
                edit.editing=true;
            }
            if(edit.page==SettingsPage::DatasetDetail)datasetAction(DatasetAction::Language,key==UiKey::Right?1:0);
            else {
                edit.edit_value=(edit.edit_value+(key==UiKey::Right?1:count-1))%count;changed();
                if(edit.page==SettingsPage::Alarm && selection_==AlarmSoundRow) {
                    action(ActionKind::PreviewAlarm);action_.alarm_settings=data_.storage.alarm.settings;
                    action_.alarm_settings.tone=static_cast<alarm::Tone>(edit.edit_value);
                    action_.alarm_identity=data_.storage.alarm.identity;
                }
            }
            return;
        }
    }
    if(screen_resources_.settings.page==SettingsPage::Alarm) {
        auto& settings=screen_resources_.settings;
        if(key==UiKey::Escape) {settings.page=SettingsPage::Root;selection_=settingsRootIndex(SettingsRootItem::Alarm);changed();return;}
        if(key==UiKey::Up){--selection_;changed();}else if(key==UiKey::Down){++selection_;changed();}
        else if(key==UiKey::Enter && !data_.storage.busy && !data_.storage.nvs_error) {
            if(selection_==AlarmReloadRow && logMapAvailable(data_))action(ActionKind::ReloadAlarm);
        }
        return;
    }

    if (screen_ == ScreenId::Settings && (screen_resources_.settings.page == SettingsPage::DatasetList || screen_resources_.settings.page == SettingsPage::DatasetDetail)) {
        const auto& view = data_.datasets();
        if (view.applying) return;
        if (key == UiKey::Escape) {
            if(dataset_borrow_.token)dataset_publishable_=false;
            if (screen_resources_.settings.page == SettingsPage::DatasetDetail) {
                screen_resources_.settings.page = SettingsPage::DatasetList; selection_ = 0;
                datasetAction(DatasetAction::Open);
            } else { screen_resources_.settings.page = SettingsPage::DatasetMenu; selection_ = static_cast<uint8_t>(screen_resources_.settings.dataset.purpose); changed(); }
            return;
        }
        if (view.loading) return;
        if(screen_resources_.settings.page==SettingsPage::DatasetList && view.error!=DatasetError::None) {
            if(key==UiKey::Enter) datasetAction(DatasetAction::Open);
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
                screen_resources_.settings.dataset.dialog_id=view.rows[selection_].id;
                if(!screen_resources_.settings.dataset.dialog_id) {dialog_.open(DialogKind::ClearDataset);changed();}
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
        if (key == UiKey::Up) { --selection_; changed(); }
        if (key == UiKey::Down) { ++selection_; changed(); }
        if (key == UiKey::Enter) {
            if (selection_ == count-1 && datasetSelectionBlock(data_,true)==DatasetError::None) datasetAction(DatasetAction::Apply);
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
            screen_resources_.settings.dataset.list_start={};screen_resources_.settings.dataset.page=0;
            screen_resources_.settings.page=SettingsPage::DatasetList;selection_=0;datasetAction(DatasetAction::Open);
        }
        return;
    }
    if(screen_==ScreenId::Settings && screen_resources_.settings.page==SettingsPage::DatasetIndex) {
        if(data_.datasets().applying)return;
        if(key==UiKey::Escape || key==UiKey::Enter) {
            if(data_.storage.busy)return;
            screen_resources_.settings.page=SettingsPage::DatasetList;selection_=0;datasetAction(DatasetAction::Open);
        }
        return;
    }
    if (screen_resources_.settings.card_info) {
        if (key == UiKey::Escape || key == UiKey::Enter) { screen_resources_.settings.card_info = false; changed(); }
        return;
    }
    if (screen_resources_.settings.page == SettingsPage::System) {
        if (key == UiKey::Escape) {
            screen_resources_.settings.usb_mode = false; screen_resources_.settings.page = SettingsPage::Root; selection_ = SettingsSystemRow; changed();
        } else if (key == UiKey::Up) { --selection_; changed(); }
        else if (key == UiKey::Down) { ++selection_; changed(); }
        else if (key == UiKey::Enter && !data_.storage.busy) {
            if (selection_ == 2 && screen_resources_.settings.usb_mode && usbEntryAllowed(data_)) {
                dialog_.open(screen_resources_.settings.usb_mode==2?DialogKind::EnterWifiMode:DialogKind::EnterUsbMode); changed();
            } else if (selection_ == SettingsSystemResetRow) {
                screen_resources_.settings.page = SettingsPage::Reset; selection_ = 0; changed();
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
            case SettingsRootItem::Alarm:screen_resources_.settings.page=SettingsPage::Alarm;break;
            default: return;
        }
        selection_ = 0; screen_resources_.settings.license_offset = 0;
        changed();
    } else if (screen_resources_.settings.page == SettingsPage::Operation && selection_ == static_cast<unsigned>(SettingId::Dataset)) {
        screen_resources_.settings.page = SettingsPage::DatasetMenu; selection_ = 0;changed();
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




void GpsUiModel::acceptDialog(DialogKind kind) {
    switch (kind) {
    case DialogKind::RegenerateWifi: case DialogKind::ExitUsbMode: case DialogKind::ExitWifiMode: break;
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
        case DialogKind::EnterWifiMode:
            action(ActionKind::Storage);
            action_.storage_operation=StorageOperation::EnterWifiMode;
            break;
        case DialogKind::EnterUsbMode:
            action(ActionKind::Storage);
            action_.storage_operation = StorageOperation::EnterUsbMode;
            break;
        case DialogKind::ClearDataset:
            datasetAction(DatasetAction::Clear);break;
        case DialogKind::RebuildIndex:case DialogKind::VerifyIndex:
            datasetAction(kind==DialogKind::RebuildIndex?DatasetAction::RebuildIndex:DatasetAction::VerifyIndex,selection_);
            screen_resources_.settings.page=SettingsPage::DatasetIndex;changed();break;
        case DialogKind::GpsControl: case DialogKind::None: return;
    }
    action_.confirmed = true;
}

void GpsUiModel::datasetAction(DatasetAction kind,uint32_t index) {
    action(ActionKind::Dataset);action_.dataset_action=kind;action_.browse_index=index;
    action_.dataset_purpose=screen_resources_.settings.dataset.purpose;action_.browse_revision=data_.datasets().revision;
    const auto& view=data_.datasets();
    action_.dataset_page=kind==DatasetAction::Open?screen_resources_.settings.dataset.page:view.page;
    switch(kind) {
        case DatasetAction::Open:action_.dataset_id=screen_resources_.settings.dataset.list_start;break;
        case DatasetAction::Older:case DatasetAction::Newer:
            if(view.count)action_.dataset_id=view.rows[kind==DatasetAction::Older?0:view.count-1].id;
            break;
        case DatasetAction::Detail:case DatasetAction::RebuildIndex:case DatasetAction::VerifyIndex:
            if(index<view.count)action_.dataset_id=view.rows[index].id;
            break;
        case DatasetAction::Clear:break;
        default:action_.dataset_id=view.id;break;
    }
    if(kind==DatasetAction::Language || kind==DatasetAction::Apply)
        std::snprintf(action_.settings.wordbook_language,sizeof(action_.settings.wordbook_language),"%s",view.info.language);
    if(dataset_borrow_.token)dataset_publishable_=false;
}

void GpsUiModel::browseAction(BrowseAction kind,uint32_t index) {
    action(ActionKind::Browse);
    action_.browse_action=kind; action_.browse_index=index; action_.browse_revision=data_.browse().revision;
}

void GpsUiModel::action(ActionKind kind, ScreenId target) {
    if (++next_action_id_ == 0) ++next_action_id_;
    action_ = {};
    action_.kind = kind; action_.action_id = next_action_id_;
    action_.screen_generation = generation_; action_.target = target;
    changed();
}

void GpsUiModel::updateScreenName() {
    if(!generation_)return;
    const uint16_t key=uint16_t(screen_);
    if(key==screen_name_key_)return;
    screen_name_key_=key;screen_name_started_ms_=last_tick_ms_;
    screen_name_active_=hasScreenName(screen_) && mode_==DisplayMode::View;
    redraw_=true;
}

void GpsUiModel::tick(uint32_t now_ms) {
    updateScreenName();
    const bool was_screen_name=screenNameVisible();
    const auto previous_second = clock_.secondsJst();
    clock_.tick(now_ms);
    if (hasRuntimeHeader(screen_,data_) && mode_ == DisplayMode::View && clock_.secondsJst() != previous_second) redraw_ = true;
    const bool was_lit = sdAccessLit();
    const bool was_dataset_notice=datasetFailureVisible();
    last_tick_ms_ = now_ms;
    if(screen_name_active_ && now_ms-screen_name_started_ms_>=ScreenNameMs)screen_name_active_=false;
    if(was_screen_name!=screenNameVisible())redraw_=true;
    // Start the visible interval when Live Place can actually show the notice.
    if(screen_!=ScreenId::LivePlace || mode_==DisplayMode::Background) dataset_notice_started_ms_=now_ms;
    if(was_dataset_notice!=datasetFailureVisible())redraw_=true;
    if (sdAccessLit() != was_lit) redraw_ = true;
    // Keep a requested scan visible; the inactivity interval starts at completion.
    // Explicit S still enters Background through the normal key handler.
    if(screen_==ScreenId::LogStatistics && data_.browse().loading) {last_input_ms_=now_ms;return;}
    if (screen_ == ScreenId::Boot || switchingMode(data_) || data_.datasets().applying || (screen_==ScreenId::Logs && data_.browse().deleting)) return;
    if (data_.message[0] && now_ms - data_.message_at_ms >= 3000u) {
        data_.message[0] = '\0';
        redraw_ = true;  // Local toast expiry does not notify App.
    }
    if (!last_input_ms_) last_input_ms_ = now_ms;
    if (data_.boot.maintenance || mode_ != DisplayMode::View || generation_ == 0 || action_.kind != ActionKind::None || !validSettings(data_.settings)) return;
    const auto timeout = ScreenTimeouts[data_.settings.screen_timeout];
    if (timeout && now_ms - last_input_ms_ >= timeout * 1000u) {
        screen_resources_.settings.editing = false; dialog_.close();
        action(ActionKind::Sleep);
    }
}

UiSnapshot GpsUiModel::snapshot() const {
    return {screen_, generation_, revision_, last_input_ms_, selection_, action_, screen_resources_.settings.page};
}

bool GpsUiModel::takeRedraw() {
    updateScreenName();
    const bool value = redraw_;
    redraw_ = false;
    return value;
}

}  // namespace geoputer

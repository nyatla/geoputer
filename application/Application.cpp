#include "utils/Console.h"
#include "Application.h"
#include "task/ui/ScreenNavigation.h"
#include "task/ui/GpsControls.h"
#include "task/ui/PlacePresentation.h"
#include <Arduino.h>
#include <esp_system.h>
#include <tusb.h>
#include <cstdio>
#include <cstring>

namespace geoputer {

bool Application::begin(bool usb_mode) {
    usb_mode_ = usb_mode;
    if (!notifications_.initialize() || !results_.initialize() || !ui_.initialize()) return false;
    if (!ui_.start("UI", 8192, 1)) return false;
    data_.settings = defaultSettings();
    data_.map.scale = config::MapViewScale;
    data_.revision = 1;
    if (usb_mode_) return beginUsb();
    data_.boot.set(BootItem::Display, BootCheckState::Passed, "240X135");
    data_.boot.set(BootItem::Tasks, BootCheckState::Checking, "STARTING");
    data_.boot.set(BootItem::GPS, BootCheckState::Checking, "DEFERRED");
    data_.storage.busy = true; // Menus may be visible before Storage/NVS is ready.
    if (!setScreen(ScreenId::Boot)) return false;
    readUiContent(); // UI sampled startup ESC before its first screen acknowledgement.
    data_.boot.set(BootItem::Settings, BootCheckState::Checking, "NVS INIT");
    data_.boot.set(BootItem::SD, BootCheckState::Checking, "MOUNTING");
    bootChanged();
    if (!publishUiContent()) return false;
    SyncReturn result{};
    StorageRequest storage_request{};
    storage_request.kind = MessageKind::InitializeStorage;
    const bool storage_started = storage_.initialize() && storage_.start("Storage", 12288, 1);
    if (!storage_started ||
        !callTask(TaskId::Storage, storage_.inbox(), storage_request, result) || result.status != OperationStatus::Ok) {
        message("STORAGE UNAVAILABLE");
        data_.boot.set(BootItem::Settings, BootCheckState::Failed, "NO STORE");
        data_.boot.set(BootItem::SD, BootCheckState::Failed, "NO STORE");
        boot_stage_ = BootStage::SelectMode;
        boot_selection_at_ = taskTimeMs();
    } else {
        applyStorage(result.storage);
        data_.boot.card(result.storage.card_state);
    }
    data_.storage.busy = boot_stage_ == BootStage::NeedSettings || boot_stage_ == BootStage::WaitSettings;
    data_.boot.set(BootItem::Tasks, storage_started ? BootCheckState::Passed : BootCheckState::Failed,
                   storage_started ? "3T/4Q" : "TASK ERR");
    Console.printf("[MEMORY] messages ui=%u gps=%u storage=%u notify=%u return=%u sd_files=%u frame_bpp=%u\n",
                  static_cast<unsigned>(sizeof(UiRequest)), static_cast<unsigned>(sizeof(GpsRequest)),
                  static_cast<unsigned>(sizeof(StorageRequest)), static_cast<unsigned>(sizeof(AppNotification)),
                  static_cast<unsigned>(sizeof(ReturnMessage)), config::SdMaxOpenFiles, config::UiColorDepth);
    bootChanged();
    if (!publishUiContent()) return false;
    return true;
}

bool Application::callUi(UiRequest& request, SyncReturn& result) {
    return callTask(TaskId::UI, ui_.inbox(), request, result);
}

bool Application::awaitSyncResult(SyncSendResult sent, SyncReturn& result) {
    if (sent != SyncSendResult::AwaitingResult) {
        Console.printf("[APP] sync send failed: %u\n", static_cast<unsigned>(sent));
        return false;
    }
    for (;;) {
        const auto received = sync_.receiveSyncResult(result, pdMS_TO_TICKS(50));
        if (received == SyncReceiveResult::Returned) return true;
        if (received == SyncReceiveResult::ProtocolError) {
            Console.println("[APP] mismatched sync return; pending request retained");
        }
        // A timeout ends this receive call, not the accepted synchronous request.
        drainNotifications();
        vTaskDelay(1);
    }
}

bool Application::setScreen(ScreenId target, uint32_t action_id, ActionResult action_result) {
    const auto previous=screen_;
    UiRequest request{};
    request.kind = MessageKind::SetUiScreen;
    auto& command = request.payload.screen;
    command.screen = target;
    command.mode = DisplayMode::View;
    command.generation = generation_ + 1;
    command.data = data_;
    prepareScreenView(command.data,target,previous);
    command.processed_action_id = action_id;
    command.action_result = action_result;
    const uint32_t sent_revision = command.data.revision;
    SyncReturn result{};
    if (!callUi(request, result) || result.status != OperationStatus::Ok) return false;
    // Commit working-resource ownership only after UI accepts the screen.
    // Old completions during the sync wait still see the original resources.
    if(map_request_.token)map_cancel_=true;
    if(map_session_.token)map_session_close_=true;
    screen_resources_.enter(screenGroup(target));
    if(screen_resources_.hasMap())screen_resources_.map()={};
    prepareScreenView(data_,target,previous);
    if(isLogBrowser(target) || isLogBrowser(previous))prepareLogScreen(target,previous);
    screen_ = target;
    mode_ = DisplayMode::View;
    ++generation_;
    if (target == ScreenId::LivePlace || target == ScreenId::Map || viewingLogPoint(target)) refreshPlace();
    ui_data_dirty_ = data_.revision != sent_revision;
    if (action_id) last_processed_action_ = action_id;
    Console.printf("[APP] screen=%s generation=%lu action=%lu stack_free=%u\n",
                  target==ScreenId::Menu ? "Menu" : target==ScreenId::Map ? "Map" : target == ScreenId::LivePlace ? "LivePlace" : target == ScreenId::Boot ? "Boot" : target==ScreenId::Logs?"Logs":target==ScreenId::LogPoint?"LogPoint":target==ScreenId::LogProperties?"LogProperties":target==ScreenId::LogStatistics?"LogStatistics":target==ScreenId::GpsLogger?"GpsLogger":target==ScreenId::UsbStorage?"UsbStorage":"Settings",
                  static_cast<unsigned long>(generation_), static_cast<unsigned long>(action_id),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    return true;
}

void Application::handleNotification(const AppNotification& event) {
    if(event.source==TaskId::Storage && event.kind==MessageKind::LogStatisticsProgress) {
        const auto& progress=event.payload.log_progress;
        if(screen_==ScreenId::LogStatistics && data_.hasBrowse() && progress.token==browse_waiting_ &&
           progress.screen_generation==generation_ && !browse_cancel_) {
            auto& view=data_.editBrowse();view.statistics.processed=progress.processed;
            view.statistics.elapsed_ms=progress.elapsed_ms;view.statistics_total=progress.total;
            ++data_.revision;ui_data_dirty_=true;
        }
        return;
    }
    if(event.source==TaskId::Storage && event.kind==MessageKind::MapCompleted) {completeMap(event.payload.map);return;}
    if(event.source==TaskId::Storage && event.kind==MessageKind::DatasetProgress) {
        if(dataset_applying_ && dataset_waiting_ && event.payload.index_progress.token==dataset_waiting_) {
            dataset_progress_=event.payload.index_progress;dataset_progress_dirty_=true;
        }
        return;
    }
    if (event.source == TaskId::Storage && event.kind == MessageKind::UsbStorageChanged) {
        applyStorage(event.payload.usb_storage); return;
    }
    if (event.source == TaskId::UI && event.kind == MessageKind::UiContentChanged) ui_changed_ = true;
    if (event.source == TaskId::Storage &&
        (event.kind == MessageKind::RgCacheStateChanged || event.kind == MessageKind::SdAccessChanged)) {
        const bool changed = event.kind == MessageKind::RgCacheStateChanged ?
            applyActivity(data_.storage.rg, event.payload.rg) : applyActivity(data_.storage.sd, event.payload.sd);
        if (changed) { ++data_.revision; ui_data_dirty_ = true; }
        return;
    }
    if (event.source == TaskId::Storage &&
        (event.kind == MessageKind::SettingsWritten || event.kind == MessageKind::ReadCompleted ||
         event.kind == MessageKind::ReadFailed || event.kind == MessageKind::StorageOperationCompleted ||
         event.kind == MessageKind::StorageError || event.kind == MessageKind::LogWritten)) {
        const auto& completed = event.payload.storage_completion;
        applyStorage(completed.snapshot);
        if (completed.read_kind == StorageReadKind::DatasetBrowser) { completeDataset(completed.dataset); return; }
        if(completed.read_kind==StorageReadKind::LogBrowse) { completeBrowser(completed.logs); return; }
        if (completed.read_kind == StorageReadKind::Log) { log_retry_at_ = taskTimeMs(); return; }
        if (completed.read_kind == StorageReadKind::Dataset) {
            if (completed.request_id != dataset_check_waiting_) return;
            dataset_check_waiting_ = 0;
            if(completed.place.state==PlaceState::Waiting) {
                if(screen_==ScreenId::Boot && boot_stage_==BootStage::WaitDataset)boot_stage_=BootStage::NeedDataset;
                else dataset_check_needed_=true;
                return;
            }
            if (applyDatasetInfo(data_.dataset, completed.place.dataset)) refreshPlace();
            if (screen_ == ScreenId::Boot && boot_stage_ == BootStage::WaitDataset && completed.request_id == BootDatasetRequest) {
                data_.boot.dataset(completed.place.state); boot_stage_ = BootStage::Complete; bootChanged();
            }
            return;
        }
        if (completed.read_kind == StorageReadKind::Settings &&
            boot_stage_ == BootStage::WaitSettings && completed.request_id == BootSettingsRequest) {
            data_.boot.settings(completed.snapshot.settings_state);
            boot_stage_ = BootStage::SelectMode;
            boot_selection_at_ = taskTimeMs();
            bootChanged();
            return;
        }
        if (completed.read_kind == StorageReadKind::Place) {
            // Only stage completion here: this handler also runs while another
            // synchronous call is waiting. UI commands run later in processPlace.
            if(place_queued_ && place_flow_.owns(completed.request_id) &&
               samePlaceRequest(place_borrow_,completed.place.request))place_completion_ready_=true;
            return;
        }
        if (completed.operation_id == storage_operation_id_) storage_operation_id_ = 0;
        if (!completed.snapshot.operation_ok) {
            if (completed.snapshot.operation == StorageOperation::EnterUsbMode || completed.snapshot.operation == StorageOperation::ExitUsbMode)
                message("MODE SWITCH FAILED");
            else message(completed.snapshot.operation == StorageOperation::SaveSettings || completed.operation_id == 0
                        ? "SETTINGS ERROR" : "SD OPERATION FAILED");
        } else if (completed.snapshot.operation == StorageOperation::EnterUsbMode || completed.snapshot.operation == StorageOperation::ExitUsbMode) {
            restart_pending_ = true;
        } else if (completed.snapshot.operation == StorageOperation::SaveSettings) message("SETTINGS SAVED");
        else if (completed.snapshot.operation == StorageOperation::Unmount) message("SAFE TO REMOVE SD");
        else if (completed.snapshot.operation == StorageOperation::Format) { dataset_check_needed_ = true; message("FAT32 FORMAT COMPLETE"); refreshPlace(); }
        else if (completed.snapshot.operation == StorageOperation::Remount) { dataset_check_needed_ = true; message("SD MOUNTED"); refreshPlace(); }
    }
    if (event.source == TaskId::GPS &&
        (event.kind == MessageKind::GpsSampleReady || event.kind == MessageKind::GpsStatusChanged)) {
        const auto old_state = data_.gps.reception;
        const bool had_fix = data_.gps.fix.valid;
        if (!applyGpsSnapshot(data_.gps, event.payload.gps)) return;
        if ((!data_.gps.fix.valid || !had_fix) && !viewingLogPoint(screen_)) refreshPlace();
        adoptLogSample();
        ++data_.revision;
        ui_data_dirty_ = true;
        // No nested synchronous UI request here, including during another task's result wait.
        if (old_state != data_.gps.reception)
            Console.printf("[GPS] %s\n", gpsReceptionName(data_.gps.reception));
        if constexpr (config::DetailedDiagnostics) {
            if (data_.gps.fix.valid) Console.printf("[GPS] FIX LAT=%.6f LON=%.6f sample=%lu\n", data_.gps.fix.latitude,
                          data_.gps.fix.longitude, static_cast<unsigned long>(data_.gps.sample_seq));
        }
    }
}

bool Application::publishUiContent(uint32_t action_id, ActionResult action_result) {
    UiRequest request{};
    request.kind = MessageKind::UpdateUiContent;
    auto& command = request.payload.update;
    command.mode = mode_;
    command.generation = generation_;
    command.data = data_;
    command.processed_action_id = action_id;
    command.action_result = action_result;
    const uint32_t sent_revision = data_.revision;
    SyncReturn result{};
    const bool accepted = callUi(request, result) && result.status == OperationStatus::Ok;
    // A newer GPS notification may arrive while this update's result is awaited.
    ui_data_dirty_ = !accepted || data_.revision != sent_revision;
    return accepted;
}

void Application::drainNotifications() {
    AppNotification event{};
    // Bound this work so result retrieval is revisited even under notification load.
    for (unsigned i = 0; i < 8 && notifications_.receive(event); ++i) handleNotification(event);
}

bool Application::readUiSnapshot(UiSnapshot& content) {
    // Release the large request/result frame before dispatching an action that
    // can send another UI request. Only the small immutable snapshot survives.
    UiRequest request{};
    request.kind = MessageKind::GetUiContent;
    SyncReturn result{};
    if (!callUi(request, result) || result.status != OperationStatus::Ok) return false;
    content = result.content;
    return true;
}

void Application::readUiContent() {
    ui_changed_ = false;
    UiSnapshot content{};
    if (!readUiSnapshot(content)) {
        ui_changed_ = true;
        vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }
    const auto& action = content.action;
    if (action.kind == ActionKind::None) return;
    if (content.screen != screen_ || action.screen_generation != generation_ || content.screen_generation != generation_) {
        if (!publishUiContent(action.action_id, ActionResult::Rejected)) ui_changed_ = true;
        return;
    }
    if (action.action_id == last_processed_action_) {
        if (!publishUiContent(action.action_id, ActionResult::Applied)) ui_changed_ = true;
        return;
    }
    if(action.kind==ActionKind::MapScale) {
        const bool accepted=changeMapScale(action.map_scale);
        last_processed_action_=action.action_id;
        if(!publishUiContent(action.action_id,accepted?ActionResult::Applied:ActionResult::Rejected))ui_changed_=true;
        return;
    }
    if (action.kind == ActionKind::Maintenance && !usb_mode_ && mode_ == DisplayMode::View &&
        (screen_ == ScreenId::Boot || (screen_ == ScreenId::LivePlace && placeIssue(data_) != PlaceState::Ready))) {
        const bool allowed = datasetChangeReady() && !restart_pending_;
        if (allowed) {
            data_.boot.maintenance = true;
            bootChanged();
            Console.println("[BOOT] maintenance requested; automatic GPS/GIS startup disabled");
        } else message("STOP LOGGER FIRST");
        last_processed_action_ = action.action_id;
        if (!publishUiContent(action.action_id, allowed ? ActionResult::Applied : ActionResult::Rejected)) ui_changed_ = true;
        if (allowed) enterMaintenance();
        return;
    }
    if (action.kind == ActionKind::RestartLogger && data_.boot.maintenance && screen_ == ScreenId::Settings &&
        content.page == SettingsPage::Root && action.confirmed && datasetChangeReady()) {
        // Do not interrupt a storage operation; its completion must be observed first.
        const bool allowed = !data_.storage.busy && !storage_operation_id_ && !dataset_waiting_;
        if (allowed) restart_pending_ = true;
        else message("STORAGE BUSY");
        last_processed_action_ = action.action_id;
        if (!publishUiContent(action.action_id, allowed ? ActionResult::Applied : ActionResult::Rejected)) ui_changed_ = true;
        return;
    }
    if (action.kind == ActionKind::Wake && mode_ == DisplayMode::Background) {
        mode_ = DisplayMode::View;
        last_processed_action_ = action.action_id;
        if (!publishUiContent(action.action_id, ActionResult::Applied)) ui_changed_ = true;
        return;
    }
    if(action.kind==ActionKind::UsbExport && screen_==ScreenId::UsbStorage && mode_==DisplayMode::View) {
        const bool ok=operateUsbExport(action);
        last_processed_action_=action.action_id;
        if(!publishUiContent(action.action_id,ok?ActionResult::Applied:ActionResult::Rejected))ui_changed_=true;
        return;
    }
    if (action.kind == ActionKind::Sleep && !data_.boot.maintenance && mode_ == DisplayMode::View) {
        mode_ = DisplayMode::Background;
        if(viewingLogPoint(screen_)) {data_.editBrowse().playing=false;++data_.revision;}
        last_processed_action_ = action.action_id;
        if (!publishUiContent(action.action_id, ActionResult::Applied)) ui_changed_ = true;
        return;
    }
    if (action.kind == ActionKind::Storage && mode_ == DisplayMode::View &&
        ((screen_ == ScreenId::UsbStorage && action.storage_operation == StorageOperation::ExitUsbMode) ||
         (screen_ == ScreenId::Settings && content.page == SettingsPage::System && content.selected_setting == 2 && action.storage_operation == StorageOperation::EnterUsbMode))) {
        const bool accepted = operateBootMode(action);
        last_processed_action_ = action.action_id;
        if (!publishUiContent(action.action_id,accepted?ActionResult::Applied:ActionResult::Rejected)) ui_changed_ = true;
        return;
    }
    if (screen_ == ScreenId::Settings && mode_ == DisplayMode::View &&
        ((action.kind == ActionKind::SaveSettings && content.page == SettingsPage::Operation) ||
         (action.kind == ActionKind::ResetSettings && content.page == SettingsPage::Reset && action.confirmed) ||
         (action.kind == ActionKind::Storage && content.page == SettingsPage::Storage))) {
        const bool accepted = operateStorage(action);
        last_processed_action_ = action.action_id;
        if (!publishUiContent(action.action_id, accepted ? ActionResult::Applied : ActionResult::Rejected)) ui_changed_ = true;
        return;
    }
    if (action.kind == ActionKind::Dataset && screen_ == ScreenId::Settings && mode_ == DisplayMode::View) {
        const bool accepted = operateDataset(action);
        last_processed_action_ = action.action_id;
        if (!publishUiContent(action.action_id, accepted ? ActionResult::Applied : ActionResult::Rejected)) ui_changed_ = true;
        return;
    }
    if (action.kind == ActionKind::Logging && mode_ == DisplayMode::View) {
        if (!queueLogAction(action)) {
            last_processed_action_ = action.action_id;
            if (!publishUiContent(action.action_id, ActionResult::Rejected)) ui_changed_ = true;
        }
        return;
    }
    if(action.kind==ActionKind::Browse && isLogBrowser(screen_) && mode_==DisplayMode::View) {
        const auto before=generation_;
        const bool accepted=operateBrowser(action);
        last_processed_action_=action.action_id;
        if(before==generation_ && !publishUiContent(action.action_id,accepted?ActionResult::Applied:ActionResult::Rejected)) ui_changed_=true;
        return;
    }
    const bool valid = !data_.boot.maintenance && action.kind == ActionKind::ChangeScreen && content.screen == screen_ &&
                       action.screen_generation == generation_ && content.screen_generation == generation_ &&
                       ((screen_ == ScreenId::Boot && data_.boot.complete && action.target == ScreenId::LivePlace) ||
                        (rootScreenTransition(screen_, content.page, action.target) && !data_.browse().deleting) ||
                        (screen_==ScreenId::LogProperties && action.target==ScreenId::Logs) ||
                        (screen_==ScreenId::LogPoint && action.target==ScreenId::LogProperties) ||
                        (screen_==ScreenId::LogStatistics && action.target==(data_.browse().statistics_from_point?ScreenId::LogPoint:ScreenId::LogProperties)));
    if (valid && action.action_id != last_processed_action_) {
        if (!setScreen(action.target, action.action_id, ActionResult::Applied)) ui_changed_ = true;
    } else {
        // Acknowledge a rejected or already-processed action without executing it again.
        if (!publishUiContent(action.action_id,
                              action.action_id == last_processed_action_ ? ActionResult::Applied : ActionResult::Rejected)) {
            ui_changed_ = true;
        }
    }
}

void Application::message(const char* value) {
    std::snprintf(data_.message, sizeof(data_.message), "%s", value);
    data_.message_at_ms = taskTimeMs();
    ++data_.revision;
    ui_data_dirty_ = true;
}

void Application::applyStorage(const StorageSnapshot& state) {
    if (!state.revision || (data_.storage.revision &&
        static_cast<int32_t>(state.revision - data_.storage.revision) <= 0)) return;
    const auto old_card = data_.storage.card_state;
    const auto old_dataset = data_.settings.dataset;
    const auto old_failure = data_.storage.dataset_failure.revision;
    const auto old_export=data_.storage.export_job.phase;
    const auto rg = data_.storage.rg;
    const auto sd = data_.storage.sd;
    const auto log = data_.storage.log;
    const auto log_result = data_.storage.log_result;
    data_.storage = state;
    if(usb_mode_ && old_export!=ExportPhase::Selecting && state.export_job.phase==ExportPhase::Selecting) {
        data_.editBrowse()={};data_.editBrowse().loading=true;++data_.editBrowse().revision;
        auto& logs=screen_resources_.logs();
        logs.request={};logs.request.kind=LogReadKind::List;logs.request.count=4;
        logs.need=true;browse_cancel_=false;
    }
    // App can have accepted more RAM records since Storage constructed this notification.
    if (applyLogSnapshot(data_.storage.log, log) ||
        (log.revision && log.revision == data_.storage.log.revision)) data_.storage.log_result = log_result;
    applyActivity(data_.storage.rg, rg);
    applyActivity(data_.storage.sd, sd);
    if (!usb_mode_ && old_card != state.card_state && state.card_state == CardState::Mounted) { refreshPlace(); }
    if (state.settings_state == SettingsState::Defaults || state.settings_state == SettingsState::Loaded ||
        state.settings_state == SettingsState::Saved || (state.dataset_failure.number && !state.settings.dataset)) data_.settings = state.settings;
    if(state.dataset_failure.revision!=old_failure && screen_==ScreenId::Boot) data_.boot.settings(state.settings_state);
    if (!data_.settings.dataset) {
        data_.dataset.state = PlaceState::Unselected;
        if (old_dataset || data_.place_state != PlaceState::Unselected) refreshPlace();
    }
    ++data_.revision;
    ui_data_dirty_ = true;
}

bool Application::operateStorage(const UiAction& action) {
    if (dataset_applying_ || data_.storage.busy || storage_operation_id_) { message("STORAGE BUSY"); return false; }
    const bool saving = action.kind == ActionKind::SaveSettings || action.kind == ActionKind::ResetSettings;
    const auto value = action.kind == ActionKind::ResetSettings ? defaultSettings() : action.settings;
    if (action.kind == ActionKind::ResetSettings &&
        (data_.settings.dataset || data_.settings.wordbook_language[0])) {
        DatasetRequest reset{}; reset.action = DatasetAction::Clear; reset.reset_settings = true;
        return requestDataset(reset);
    }
    if (saving && (!validSettings(value) || value.gps_mode != 0 || value.dataset != data_.settings.dataset ||
                   std::strcmp(value.wordbook_language, data_.settings.wordbook_language))) {
        message("SETTING NOT AVAILABLE"); return false;
    }
    if (!saving && (data_.storage.log.open || data_.storage.log.closing || data_.storage.log.buffered || log_control_.action_id)) {
        message("STOP LOGGER FIRST"); return false;
    }
    if (!saving && (action.storage_operation == StorageOperation::None ||
                    action.storage_operation == StorageOperation::SaveSettings ||
                    (action.storage_operation == StorageOperation::Format && !action.confirmed))) {
        message("INVALID SD OPERATION"); return false;
    }
    StorageRequest request{};
    request.kind = saving ? MessageKind::SaveSettings : MessageKind::OperateStorage;
    request.payload.storage_command = {saving ? StorageOperation::SaveSettings : action.storage_operation,
                                       value, action.action_id, action.confirmed};
    SyncReturn result{};
    storage_operation_id_ = action.action_id;
    if (!callTask(TaskId::Storage, storage_.inbox(), request, result) || result.status != OperationStatus::Ok) {
        storage_operation_id_ = 0;
        message(saving ? "SETTINGS SAVE REJECTED" : "SD OPERATION REJECTED");
        return false;
    }
    applyStorage(result.storage);
    if (saving) { data_.settings = value; ++data_.revision; ui_data_dirty_ = true; }
    if (storage_operation_id_) message(saving ? "SAVING SETTINGS..." : "SD WORKING...");
    return true;
}

void Application::refreshPlace() {
    place_flow_.refresh();
    clearPlacePresentation(data_, !data_.settings.dataset ? PlaceState::Unselected :
                           datasetInactive(data_.dataset.state) ? data_.dataset.state :
                           !displayedFix(screen_,data_).valid ? PlaceState::NoFix :
                           data_.storage.card_state != CardState::Mounted ? PlaceState::NoCard : PlaceState::Waiting);
    ++data_.revision;
    ui_data_dirty_ = true;
}

bool Application::canSearchPlace() const {
    return data_.settings.dataset && !data_.boot.maintenance && (screen_ == ScreenId::LivePlace || screen_ == ScreenId::Map ||
            (viewingLogPoint(screen_) && data_.browse().point_loaded && !data_.browse().loading && data_.browse().error==LogReadError::None)) &&
           mode_ == DisplayMode::View && displayedFix(screen_,data_).valid && !browse_waiting_ &&
           !map_request_.token && !place_borrow_.token &&
           data_.storage.card_state == CardState::Mounted && !data_.storage.busy &&
           data_.dataset.state == PlaceState::Ready && !dataset_check_needed_ && !dataset_check_waiting_;
}

void Application::requestPlace() {
    if (!canSearchPlace()) return;
    const auto& fix=displayedFix(screen_,data_);
    const uint32_t sample=viewingLogPoint(screen_)?data_.browse().index+1:data_.gps.sample_seq;
    if (place_flow_.sameCell(fix,data_.dataset.unit_inv_x,data_.dataset.unit_inv_y)) {
        // The area is already known: keep the main-screen coordinates current
        // without reading code pixels, touching the name LRU or issuing SD work.
        if(data_.place_fix.latitude!=fix.latitude || data_.place_fix.longitude!=fix.longitude) {
            data_.place_fix=fix;++data_.revision;ui_data_dirty_=true;
        }
        return;
    }
    if (place_flow_.waitMs(sample) != 0) return;
    StorageRequest request{};
    request.kind = MessageKind::SearchPlace;
    request.source = TaskId::Application;
    if (++place_request_id_ == 0) ++place_request_id_;
    request.request_id = place_request_id_;
    request.sent_at_ms = taskTimeMs();
    auto& search=request.payload.place_request;
    search={generation_,sample,fix};search.token=request.request_id;
    search.dataset_generation=data_.dataset.generation;search.work_bytes=config::IndexReadBufferBytes;
    if(!placeUiCommand(MessageKind::BeginPlaceSearch,search,&search.buffer))return;
    place_borrow_=search;place_queued_=place_discard_=false;
    // A queue-full or changed dataset still leaves a loan to return to UI.
    if(search.dataset_generation!=data_.dataset.generation ||
       sendSpeculative(storage_.inbox(),request)!=SpeculativeResult::Queued) {
        place_completion_ready_=true;place_discard_=true;return;
    }
    place_completion_ready_=false;place_queued_=true;
    place_flow_.sent(request.request_id,search);
    beginPlacePresentation(data_,search);
    ++data_.revision;ui_data_dirty_=true;
}

bool Application::placeUiCommand(MessageKind kind,const PlaceRequest& request,map::BufferLease* buffer) {
    UiRequest command{};command.kind=kind;command.payload.place=request;
    SyncReturn result{};
    if(!callUi(command,result) || result.status!=OperationStatus::Ok)return false;
    if(buffer)*buffer=result.map_buffer;
    return true;
}

void Application::processPlace() {
    if(!place_borrow_.token || !place_completion_ready_)return;
    if(!place_queued_) {
        if(!placeUiCommand(MessageKind::DiscardPlaceSearch,place_borrow_))return;
    } else {
        // Reader use ended before Storage sent completion. Copy the fixed-size
        // result before UI commit destroys it and returns the workspace.
        const PlaceResult place=*placeResultMemory(place_borrow_);
        const bool valid=samePlaceRequest(place.request,place_borrow_);
        const bool accepted=valid && applyDatasetInfo(data_.dataset,place.dataset);
        const bool current=valid && place_flow_.accepts(place_borrow_.token,place.request.screen_generation,generation_);
        const bool publish=!place_discard_ && current && accepted && !datasetInactive(data_.dataset.state) &&
            !datasetInactive(place.state) && mode_==DisplayMode::View &&
            (screen_==ScreenId::LivePlace || screen_==ScreenId::Map || viewingLogPoint(screen_)) && displayedFix(screen_,data_).valid &&
            (!viewingLogPoint(screen_) || (!data_.browse().loading && place.request.sample_seq==data_.browse().index+1));
        bool committed=false;
        if(publish)committed=placeUiCommand(MessageKind::CommitPlaceSearch,place_borrow_);
        if(!committed) {
            place_discard_=true;
            if(!placeUiCommand(MessageKind::DiscardPlaceSearch,place_borrow_))return;
        }
        place_flow_.completed(place_borrow_.token,place_borrow_.screen_generation,generation_);
        if(committed) {
            // Keep App's normal UI snapshot coherent with the UI's commit;
            // subsequent clock/GPS updates cannot restore an older name.
            completePlacePresentation(data_,place);
            Console.printf("[APP] place=%s / %s (%s)\n",data_.place[0],data_.place[1],placeStateName(place.state));
        } else place_flow_.refresh();
        if(accepted && datasetInactive(data_.dataset.state))refreshPlace();
        ++data_.revision;ui_data_dirty_=true;
    }
    place_borrow_={};place_completion_ready_=place_queued_=place_discard_=false;
}

void Application::bootChanged() {
    ++data_.revision;
    ui_data_dirty_ = true;
}

void Application::initializeGps() {
    GpsRequest request{};
    request.kind = MessageKind::StartGpsReception;
    SyncReturn result{};
    const bool gps_started = gps_.initialize() && gps_.start("GPS", 4096, 2);
    if (!gps_started ||
        !callTask(TaskId::GPS, gps_.inbox(), request, result) || result.status != OperationStatus::Ok) {
        data_.gps = {};
        data_.gps.reception = GpsReception::Unavailable;
        ++data_.revision;
        ui_data_dirty_ = true;
        Console.println("[APP] GPS initialization failed; UI remains available");
        data_.boot.set(BootItem::GPS, BootCheckState::Failed, "UART ERR");
    } else {
        applyGpsSnapshot(data_.gps, result.gps);
        data_.boot.set(BootItem::GPS, BootCheckState::Passed, "UART OK");
    }
    const bool tasks_ok = gps_started && data_.boot.checks[static_cast<unsigned>(BootItem::Tasks)].state == BootCheckState::Passed;
    data_.boot.set(BootItem::Tasks, tasks_ok ? BootCheckState::Passed : BootCheckState::Failed, tasks_ok ? "4T/5Q" : "TASK ERR");
    bootChanged();
}

void Application::enterMaintenance() {
    const bool loading_settings = boot_stage_ == BootStage::NeedSettings || boot_stage_ == BootStage::WaitSettings;
    if (screen_ == ScreenId::Settings) {
        if (!loading_settings) boot_stage_ = BootStage::Complete;
        return;
    }
    // Only pending NVS restoration may continue; do not start GPS/GIS.
    // A running Storage job retains ownership while the menus are displayed.
    if (!loading_settings) boot_stage_ = BootStage::Complete;
    if (screen_ == ScreenId::Boot) {
        if (data_.boot.checks[static_cast<unsigned>(BootItem::GPS)].state == BootCheckState::Checking)
            data_.boot.set(BootItem::GPS, BootCheckState::Warning, "SKIPPED");
        if (data_.boot.checks[static_cast<unsigned>(BootItem::Dataset)].state == BootCheckState::Pending)
            data_.boot.set(BootItem::Dataset, BootCheckState::Warning, "SKIPPED");
    }
    bootChanged();
    if (setScreen(ScreenId::Settings)) Console.println("[BOOT] maintenance settings ready; automatic GPS logging/GIS disabled");
}

void Application::advanceBoot() {
    if (data_.boot.maintenance && boot_stage_ != BootStage::NeedSettings && boot_stage_ != BootStage::WaitSettings) {
        enterMaintenance();
        return;
    }
    // Maintenance is shown immediately; only NVS loading continues behind it.
    if (screen_ != ScreenId::Boot && !data_.boot.maintenance) return;
    if (boot_stage_ == BootStage::SelectMode) {
        if (taskTimeMs() - boot_selection_at_ < config::BootMaintenanceWindowMs) return;
        initializeGps();
        boot_stage_ = BootStage::NeedDataset;
        // A key may have arrived while waiting for GPS's synchronous result.
        if (ui_changed_) return;
    }
    if (boot_stage_ == BootStage::NeedSettings || boot_stage_ == BootStage::NeedDataset) {
        const bool settings = boot_stage_ == BootStage::NeedSettings;
        StorageRequest request{};
        request.kind = settings ? MessageKind::LoadSettings : MessageKind::CheckDataset;
        request.source = TaskId::Application;
        request.request_id = settings ? BootSettingsRequest : BootDatasetRequest;
        request.sent_at_ms = taskTimeMs();
        if (sendSpeculative(storage_.inbox(), request) == SpeculativeResult::Queued) {
            boot_stage_ = settings ? BootStage::WaitSettings : BootStage::WaitDataset;
            if (!settings) dataset_check_waiting_ = request.request_id;
            data_.boot.set(settings ? BootItem::Settings : BootItem::Dataset, BootCheckState::Checking,
                           settings ? "READING" : "OPENING");
            data_.storage.busy = true;
            bootChanged();
        }
    }
    if (boot_stage_ == BootStage::Complete && !data_.boot.complete && data_.boot.finish()) {
        boot_completed_at_ = taskTimeMs();
        bootChanged();
        for (unsigned i = 0; i < BootStatus::Count; ++i)
            Console.printf("[BOOT] %s: %s\n", bootItemName(i), data_.boot.checks[i].detail);
        Console.println(data_.boot.hasIssues() ? "[BOOT] Ready with warnings" : "[BOOT] Ready");
    }
}

void Application::processDatasetCheck() {
    if (!dataset_check_needed_ || dataset_check_waiting_ || screen_ == ScreenId::Boot ||
        data_.storage.busy || restart_pending_ || dataset_applying_) return;
    StorageRequest request{}; request.kind = MessageKind::CheckDataset; request.source = TaskId::Application;
    if (++place_request_id_ == 0) ++place_request_id_;
    request.request_id = place_request_id_; request.sent_at_ms = taskTimeMs();
    if (sendSpeculative(storage_.inbox(), request) != SpeculativeResult::Queued) return;
    dataset_check_needed_ = false; dataset_check_waiting_ = request.request_id;
    data_.storage.busy = true; ++data_.revision; ui_data_dirty_ = true;
}

void Application::updateLogElapsed() {
    const auto now = taskTimeMs();
    if (data_.recording.now_ms / 1000 != now / 1000 && screen_ == ScreenId::GpsLogger) { ++data_.revision; ui_data_dirty_ = true; }
    data_.recording.now_ms = now;
    if (data_.recording.state == RecordingState::Recording) {
        const auto old_seconds = data_.recording.elapsed_ms / 1000;
        data_.recording.elapsed_ms += now - log_elapsed_at_;
        if (old_seconds != data_.recording.elapsed_ms / 1000) { ++data_.revision; ui_data_dirty_ = true; }
    }
    log_elapsed_at_ = now;
}

void Application::adoptLogSample() {
    const auto& gps = data_.gps;
    if (gps.sample_seq == log_seen_seq_) return;
    log_seen_seq_ = gps.sample_seq;
    if (!log_accepting_ || !gps.fix.valid || gps.reception != GpsReception::Fix) return;
    const auto now = taskTimeMs();
    if (log_adopted_ && now - log_adopt_at_ < LogIntervals[data_.settings.log_interval] * 1000u) return;
    log_adopt_at_ = now; log_adopted_ = true;
    if (log_pending_) {
        // The previous adopted point remains owned by App until accepted.
        ++data_.recording.dropped;
    } else {
        log_pending_record_ = {};
        log_pending_record_.session_id = data_.storage.log.session_id;
        log_pending_record_.sample_seq = gps.sample_seq;
        log_pending_record_.fix = gps.fix;
        log_pending_ = true; data_.recording.pending = true;
        // Also runs while another synchronous control call is waiting for its return.
        if (!appendPendingLog()) log_retry_at_ = now + 1000;
    }
    ++data_.revision; ui_data_dirty_ = true;
}

bool Application::appendPendingLog() {
    LogSnapshot snapshot;
    const auto error = storage_.tryAppendLog(log_pending_record_, snapshot);
    if (applyLogSnapshot(data_.storage.log, snapshot)) { ++data_.revision; ui_data_dirty_ = true; }
    data_.storage.log_result = error == LogError::None ? snapshot.error : error;
    if (error != LogError::None) { message(logErrorName(error)); return false; }
    log_pending_ = false; data_.recording.pending = false;
    log_retry_at_ = taskTimeMs();
    if (data_.recording.state == RecordingState::Starting) {
        data_.recording.state = RecordingState::Recording;
        log_elapsed_at_ = taskTimeMs();
    }
    ++data_.revision; ui_data_dirty_ = true;
    return true;
}

bool Application::logRequest(MessageKind kind, const LogCommand& command) {
    StorageRequest request{};
    request.kind = kind; request.payload.log = command;
    SyncReturn result{};
    if (!callTask(TaskId::Storage, storage_.inbox(), request, result)) return false;
    applyStorage(result.storage);
    if (result.status != OperationStatus::Ok) {
        message(logErrorName(result.storage.log_result)); return false;
    }
    return true;
}

bool Application::queueLogAction(const UiAction& action) {
    if (data_.boot.maintenance || dataset_applying_ || restart_pending_) return false;
    if (log_control_.action_id) return action.action_id == log_control_.action_id;
    const auto state = data_.recording.state;
    const bool source = screen_ != ScreenId::Boot && screen_ != ScreenId::UsbStorage && action.confirmed && gpsControlAllowed(data_, action.log_action);
    const bool valid = source && (
        (action.log_action == LogAction::Start && state == RecordingState::Stopped) ||
        (action.log_action == LogAction::Resume && state == RecordingState::Paused) ||
        (action.log_action == LogAction::Pause && (state == RecordingState::Recording || state == RecordingState::Starting)) ||
        (action.log_action == LogAction::Stop));
    if (!valid) return false;
    log_control_ = action;
    if (action.log_action == LogAction::Pause || action.log_action == LogAction::Stop) log_accepting_ = false;
    return true;
}

bool Application::ensureGpsReception() {
    GpsRequest request{}; request.kind = MessageKind::StartGpsReception;
    SyncReturn result{};
    if (!callTask(TaskId::GPS, gps_.inbox(), request, result) || result.status != OperationStatus::Ok) {
        message("GPS CONTROL FAILED"); return false;
    }
    applyGpsSnapshot(data_.gps, result.gps);
    refreshPlace();
    return true;
}

void Application::processLog() {
    const auto now = taskTimeMs();
    if (static_cast<int32_t>(now - log_retry_at_) < 0) return;
    if (log_pending_) {
        if (!appendPendingLog()) { log_retry_at_ = now + 1000; return; }
    }
    if (!log_control_.action_id) return;
    const auto action = log_control_;
    LogCommand command{};
    command.session_id = data_.storage.log.session_id;
    command.sample_seq = data_.storage.log.accepted_seq;
    bool ok = true;
    updateLogElapsed();
    if (action.log_action == LogAction::Start) {
        ok = ensureGpsReception();
        command.session_id = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
        if (!command.session_id) command.session_id = 1;
        command.created_utc_ms = (data_.gps.fix.fields & GpsTime) ? data_.gps.fix.utc_epoch_ms : UINT64_MAX;
        if (ok) ok = logRequest(MessageKind::OpenLogSession, command);
        if (ok) {
            data_.recording = {};
            data_.recording.state = RecordingState::Starting;
            log_adopted_ = false; log_accepting_ = true;
            // Start with the next GNSS epoch, never with a stale pre-start Fix.
            log_seen_seq_ = data_.gps.sample_seq;
        }
    } else if (action.log_action == LogAction::Resume) {
        data_.recording.state = RecordingState::Starting;
        log_accepting_ = true; log_adopted_ = false; log_seen_seq_ = data_.gps.sample_seq;
    } else {
        if (data_.recording.state != RecordingState::Stopped)
            ok = logRequest(action.log_action == LogAction::Stop ? MessageKind::CloseLogSession : MessageKind::FlushLog, command);
        if (ok) {
            data_.recording.state = action.log_action == LogAction::Stop ? RecordingState::Stopped : RecordingState::Paused;
        }
        else log_accepting_ = data_.recording.state == RecordingState::Recording || data_.recording.state == RecordingState::Starting;
    }
    log_elapsed_at_ = taskTimeMs();
    data_.recording.now_ms = log_elapsed_at_;
    ++data_.revision; ui_data_dirty_ = true;
    log_control_ = {};
    last_processed_action_ = action.action_id;
    if (!publishUiContent(action.action_id, ok ? ActionResult::Applied : ActionResult::Rejected)) ui_changed_ = true;
    Console.printf("[APP] logger state=%u accepted=%lu saved=%lu dropped=%lu\n", static_cast<unsigned>(data_.recording.state),
                  static_cast<unsigned long>(data_.storage.log.accepted), static_cast<unsigned long>(data_.storage.log.saved),
                  static_cast<unsigned long>(data_.recording.dropped));
}

void Application::runOnce() {
    drainNotifications();
    processDataset();
    processPlace();
    if (restart_pending_) {
        message("RESTARTING...");
        publishUiContent();
        // The next boot may advertise a different set of USB interfaces.
        // Let the host observe removal before restarting the controller.
        tud_disconnect();
        vTaskDelay(pdMS_TO_TICKS(150));
        ESP.restart();
        return;
    }
    if (usb_mode_) {
        processBrowser();
        if (ui_changed_) readUiContent();
        else if (ui_data_dirty_) publishUiContent();
        else { AppNotification event{}; if (notifications_.receive(event,pdMS_TO_TICKS(50))) handleNotification(event); }
        return;
    }
    updateLogElapsed();
    processLog();
    if (ui_changed_) {
        readUiContent();
        return;
    }
    advanceBoot();
    processDatasetCheck();
    processBrowser();
    requestPlace();
    processMap();
    processReplay();
    if (ui_data_dirty_) {
        if (!publishUiContent()) vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }
    if (screen_ == ScreenId::Boot && data_.boot.complete && taskTimeMs() - boot_completed_at_ >= data_.boot.holdMs()) {
        if (!setScreen(ScreenId::LivePlace)) vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }
    AppNotification event{};
    TickType_t wait = portMAX_DELAY;
    if (screen_ == ScreenId::Boot) {
        if (data_.boot.complete) {
            const auto elapsed = taskTimeMs() - boot_completed_at_;
            const auto hold = data_.boot.holdMs();
            wait = elapsed < hold ? pdMS_TO_TICKS(hold - elapsed) : 0;
        } else if (boot_stage_ == BootStage::NeedSettings || boot_stage_ == BootStage::SelectMode || boot_stage_ == BootStage::NeedDataset) {
            wait = pdMS_TO_TICKS(50);  // Only retry requests that were not queued.
        }
    } else if (canSearchPlace()) {
        const auto remaining = place_flow_.waitMs(viewingLogPoint(screen_)?data_.browse().index+1:data_.gps.sample_seq,
            displayedFix(screen_,data_),data_.dataset.unit_inv_x,data_.dataset.unit_inv_y);
        if (remaining != UINT32_MAX) wait = pdMS_TO_TICKS(remaining ? remaining : 50);
    }
    if(showingMap(screen_,data_) || map_request_.token || map_session_close_ || (viewingLogPoint(screen_) && data_.browse().playing)) {if(wait==portMAX_DELAY || wait>pdMS_TO_TICKS(20))wait=pdMS_TO_TICKS(20);}
    if (data_.recording.state != RecordingState::Stopped || log_pending_ || log_control_.action_id) {
        const TickType_t log_wait = pdMS_TO_TICKS(100);
        if (wait == portMAX_DELAY || wait > log_wait) wait = log_wait;
    }
    if(dataset_pending_ || dataset_ready_ || dataset_progress_dirty_) {if(wait==portMAX_DELAY || wait>pdMS_TO_TICKS(20))wait=pdMS_TO_TICKS(20);}
    if(dataset_check_needed_ && !dataset_check_waiting_ && data_.storage.card_state == CardState::Mounted) { if(wait==portMAX_DELAY || wait>pdMS_TO_TICKS(50)) wait=pdMS_TO_TICKS(50); }
    if(browse_cancel_ || browse_release_generation_ || (screen_resources_.hasLogs() && screen_resources_.logs().need && !browse_waiting_)) { if(wait==portMAX_DELAY || wait>pdMS_TO_TICKS(50)) wait=pdMS_TO_TICKS(50); }
    if (notifications_.receive(event, wait)) handleNotification(event);
}

}  // namespace geoputer

#include "../../utils/Console.h"
#include "GpsStorageTask.h"
#include "../../hardware/CardSpi.h"
#include "../../utils/ElapsedTime.h"
#include "../../utils/HeapDiagnostics.h"
#include "DatasetSelection.h"
#include "dataset/DatasetIndex.h"
#include "dataset/DatasetReadMemory.h"
#include <SD.h>
#include "StorageCard.h"
#include "StorageFs.h"
#include "StorageDirectories.h"
#include "alarm/AlarmFile.h"
#include <esp_heap_caps.h>
#include <cstring>
#include <algorithm>
#include "../../utils/SyncClient.h"
#include "../../BootMode.h"
#include "../../hardware/LoraSafety.h"

namespace geoputer {

LogError GpsStorageTask::tryAppendLog(const LogCommand& command, LogSnapshot& snapshot) {

    const auto error = gpsState().log_writer_.append(command, taskTimeMs());
    snapshot = logSnapshot();
    return error;
}

void GpsStorageTask::closeCard() {
    SdAccessScope access(this);
    if(auto* map=work().activeMap())map->release(map->info().request);
    {
        gpsState().screen_resources_.clearMaps();gpsState().place_lookup_.close();
    }
    if (drive_ == 0xFF) return;
    unmountStorageCard(drive_);
    releaseStorageCard(drive_);
    drive_ = 0xFF;
    mounted_ = false;
}

void GpsStorageTask::mountCard() {
    SdAccessScope access(this);
    closeCard();
    state_.card_state = CardState::NoCard;
    state_.card_bytes = state_.total_bytes = state_.free_bytes = 0;
    state_.fat_type = 0;
    drive_ = initStorageCard(12, &spi_, config::SdSpiHz);
    if (drive_ == 0xFF) { state_.sd_error = state_.error = -10; return; }
    {
        gpsState().place_lookup_.setDrive(drive_);
        gpsState().log_writer_.setDrive(drive_);
        work().setDrive(drive_);
    }
    int mount_result=FR_INT_ERR;
    mounted_ = mountStorageCard(drive_, "/sd", config::SdMaxOpenFiles, false,&mount_result);  // Never format on mount failure.
    const auto type = sdcard_type(drive_);
    if (type == CARD_SD || type == CARD_SDHC || type == CARD_MMC) {
        state_.card_bytes = static_cast<uint64_t>(sdcard_num_sectors(drive_)) * 512;
        state_.card_state = CardState::Unsupported;
    }
    if (!mounted_) {
        state_.card_state=mount_result==FR_NO_FILESYSTEM?CardState::Unsupported:CardState::Failed;
        state_.sd_error=state_.error=mount_result;
        return;
    }
    char volume[] = {static_cast<char>('0' + drive_), ':', '\0'};
    FATFS* fs = nullptr;
    DWORD free_clusters = 0;
    const auto result = f_getfree(volume, &free_clusters, &fs);
    if (result != FR_OK || !fs) {
        state_.card_state = CardState::Failed;
        state_.sd_error = state_.error = static_cast<int32_t>(result);
        return;
    }
    state_.fat_type = fs->fs_type == FS_FAT32 ? 32 : fs->fs_type == FS_FAT16 ? 16 : 12;
    state_.total_bytes = static_cast<uint64_t>(fs->n_fatent - 2) * fs->csize * 512;
    state_.free_bytes = static_cast<uint64_t>(free_clusters) * fs->csize * 512;
    state_.card_state = fs->fs_type == FS_FAT32 ? CardState::Mounted : CardState::Unsupported;
    state_.sd_error = state_.error = 0;
    if(state_.card_state==CardState::Mounted) {
        const auto layout=storage_directories::check();
        if(layout==storage_directories::State::Missing)state_.card_state=CardState::NeedsSetup;
        else if(layout==storage_directories::State::Error) {
            state_.card_state=CardState::Failed;
            state_.sd_error=state_.error=-12;
        }
    }
}

bool GpsStorageTask::prepareDirectories() {
    SdAccessScope access(this);
    if(!mounted_ || (state_.card_state!=CardState::NeedsSetup && state_.card_state!=CardState::Mounted))return false;
    if(!storage_directories::create())return false;
    state_.card_state=CardState::Mounted;
    state_.sd_error=state_.error=0;
    return true;
}

bool GpsStorageTask::formatCard() {
    SdAccessScope access(this);
    // Only reached via a confirmed command. No automatic formatting or NVS erasure.
    closeCard();
    drive_ = initStorageCard(12, &spi_, config::SdSpiHz);
    if (drive_ == 0xFF) { state_.sd_error = state_.error = -10; return false; }
    char volume[] = {static_cast<char>('0' + drive_), ':', '\0'};
    uint8_t work[4096];
    // MBR partitioning keeps a freshly formatted card usable by M5Launcher.
    const auto result = f_mkfs(volume, FM_FAT32, 0, work, sizeof(work));
    closeCard();
    if (result != FR_OK) {
        state_.card_state = CardState::Failed;
        state_.total_bytes = state_.free_bytes = 0;
        state_.fat_type = 0;
        state_.sd_error = state_.error = static_cast<int32_t>(result);
        return false;
    }
    mountCard();
    return state_.card_state == CardState::Mounted ||
           (state_.card_state == CardState::NeedsSetup && prepareDirectories());
}

void GpsStorageTask::configureDataset() {

    state_.alarm={};
    state_.selected_dataset = gpsState().selected_dataset_.id;
    gpsState().screen_resources_.clearMaps();
    gpsState().place_lookup_.configure(gpsState().selected_dataset_.id.view(),
                            state_.settings.wordbook_language);
}

void GpsStorageTask::releaseDataset(PlaceState reason) {
    if (!releaseUnusableDataset(settings_store_, state_, reason)) return;
    gpsState().selected_dataset_.id = {};
    configureDataset();
    Console.printf("[DATASET RELEASE] reason=%s saved=%u\n",
                   placeStateName(reason),!state_.dataset_failure.save_failed);
}

bool GpsStorageTask::loadSettings() {
    state_.settings_state = settings_store_.load(state_.settings,gpsState().selected_dataset_.id);
    configureDataset();
    if (state_.settings_state != SettingsState::Loaded && state_.settings_state != SettingsState::Defaults) {
        state_.nvs_error = state_.error = -20;
        return false;
    }
    state_.nvs_error = 0;
    return true;
}

bool GpsStorageTask::saveSettings(const Settings& value,const DatasetId* id) {
    // One versioned, checked record; never clear the namespace or the NVS partition.
    const DatasetId& selected=id?*id:gpsState().selected_dataset_.id;
    if (!settings_store_.save(value,selected)) {
        state_.settings_state = SettingsState::Failed;
        state_.nvs_error = state_.error = -21;
        return false;
    }
    const bool selection_changed = state_.settings.dataset != value.dataset ||
        gpsState().selected_dataset_.id!=selected ||
        std::strcmp(state_.settings.wordbook_language, value.wordbook_language);
    state_.settings = value;
    state_.selected_dataset = selected;
    state_.dataset_failure.save_failed = false;
    if (selection_changed) {
        gpsState().place_lookup_.configure(selected.view(),value.wordbook_language);
        state_.alarm={};gpsState().screen_resources_.clearMaps();
    }
    state_.settings_state = SettingsState::Saved;
    state_.nvs_error = 0;
    return true;
}

void GpsStorageTask::handle(const StorageRequest& request) {
    if (request.source != TaskId::Application || request.request_id == 0) return;
    if(request.kind==MessageKind::ReleaseLogBrowse) {
        browse_release_generation_=std::max(browse_release_generation_,request.payload.log_read.screen_generation);
        return;
    }
    if(request.kind==MessageKind::CancelLogRead) {
        if(has_read_ && work().logs().result().request.kind!=LogReadKind::Delete && request.payload.log_read.token==work().logs().result().request.token) work().logs().cancel();
        return;
    }
    if (request.kind == MessageKind::LoadSettings || request.kind == MessageKind::SearchPlace || request.kind == MessageKind::CheckDataset) {
        if (!hasJob() && !replies_.completionPending() && !work().mapBusy()) {
            job_kind_ = request.kind == MessageKind::LoadSettings ? JobKind::LoadSettings :
                        request.kind == MessageKind::SearchPlace ? JobKind::SearchPlace : JobKind::CheckDataset;
            gpsState().place_request_ = request.kind == MessageKind::SearchPlace ? request.payload.place_request : PlaceRequest{};
            job_ = {}; job_request_id_ = request.request_id;
            state_.busy = true;
        }
        return;
    }
    auto& pending_return = replies_.beginReturn();
    prepareSyncReturn(pending_return, TaskId::Storage, request, ++sequence_);
    auto& result = pending_return.payload.result;
    if(request.kind==MessageKind::RequestMap) {
        if(initialized_ && !hasJob() && !replies_.completionPending() && !has_read_ && !work().mapBusy()) {
            const auto& command=request.payload.map;
            gpsState().screen_resources_.selectMap(command.session);
            auto* map=work().forMap();
            if(map && map->start(command,gpsState().screen_resources_.session(command.session))) {
                if(state_.card_state!=CardState::Mounted)map->fail(map::Error::NoCard);
                else if(!state_.settings.dataset)map->fail(map::Error::NoDataset);
                else if(command.dataset_generation!=gpsState().place_lookup_.datasetInfo().generation)map->fail(map::Error::Stale);
                result.status=OperationStatus::Ok;
            }
        }
    } else if(request.kind==MessageKind::ReleaseMap || request.kind==MessageKind::CancelMap) {
        auto* map=work().activeMap();
        if(!map || map->release(request.payload.map)) {
            result.status=OperationStatus::Ok;
            reportMapHeap(request.kind==MessageKind::ReleaseMap?"released-retained":"cancelled",request.payload.map.token,
                gpsState().screen_resources_.retainedMapBytes());
        }
    } else if(request.kind==MessageKind::CloseMapSession) {
        if(!work().mapBusy()) {
            // UI detaches; Storage keeps the fixed raster and its pixel key.
            // A new screen may render it again when the dataset/viewport match.
            result.status=OperationStatus::Ok;reportMapHeap("session-closed",request.payload.map.token);
        }

    } else if (request.kind == MessageKind::DatasetOperation) {
        if (initialized_ && !has_read_ && !hasJob() && !replies_.completionPending() && !work().mapBusy() && validDatasetMemory(request.payload.dataset)) {
            gpsState().dataset_request_ = request.payload.dataset;
            gpsState().dataset_request_.language[sizeof(gpsState().dataset_request_.language)-1] = 0;
            job_kind_ = JobKind::BrowseDatasets;
            job_ = {}; job_request_id_ = request.request_id;
            state_.busy = true; ++state_.revision; result.status = OperationStatus::Ok;
        } else result.status = OperationStatus::Unavailable;
    } else if (request.kind == MessageKind::InitializeStorage) {
        if (!initialized_) {
            initializeCardHardware();
            nvs_ready_ = settings_store_.begin();
            state_.settings = defaultSettings();
            configureDataset();
            state_.settings_state = nvs_ready_ ? SettingsState::Defaults : SettingsState::Unavailable;
            state_.nvs_error = nvs_ready_ ? 0 : -20;
            mountCard();
            state_.revision = 1;
            initialized_ = true;
            Console.printf("[STORAGE] NVS=%s SD=%s spi_hz=%lu spi_actual_hz=%lu\n", nvs_ready_ ? "ready" : "error", cardStateName(state_.card_state), static_cast<unsigned long>(config::SdSpiHz), static_cast<unsigned long>(spiClockDivToFrequency(spi_.getClockDivider())));
        }
        result.status = OperationStatus::Ok;  // Individual backend outcomes are in the snapshot.
    } else if (request.kind == MessageKind::GetStorageStatus) {
        result.status = initialized_ ? OperationStatus::Ok : OperationStatus::Unavailable;
    } else if(request.kind==MessageKind::ListLogs || request.kind==MessageKind::ReadLogSummary || request.kind==MessageKind::ReadLogRecords || request.kind==MessageKind::DeleteLog || request.kind==MessageKind::ReadLogStatistics) {
        if(initialized_ && !has_read_ && !hasJob() && !replies_.completionPending() && !work().mapBusy()) {
            auto read=request.payload.log_read;
            read.kind=request.kind==MessageKind::ReadLogStatistics?LogReadKind::Statistics:request.kind==MessageKind::ListLogs?LogReadKind::List:request.kind==MessageKind::ReadLogSummary?LogReadKind::Summary:request.kind==MessageKind::DeleteLog?LogReadKind::Delete:LogReadKind::Records;
            auto* logs=work().forLogs();
            if(logs && logs->begin(read)) {
                has_read_=true; read_prepared_=false; read_request_id_=request.request_id;statistics_progress_at_=taskTimeMs();
                const auto log=logSnapshot(); read_barrier_session_=log.session_id; read_barrier_=log.accepted;
                state_.busy=true; ++state_.revision; result.status=OperationStatus::Ok;
            }
        } else result.status=OperationStatus::Unavailable;
    } else if (initialized_ && (request.kind == MessageKind::OpenLogSession || request.kind == MessageKind::FlushLog || request.kind == MessageKind::CloseLogSession)) {
        const auto& command = request.payload.log;
        LogError error = LogError::Invalid;
        if (state_.card_state != CardState::Mounted) error = LogError::Io;
        else if (request.kind == MessageKind::OpenLogSession) {
            error = gpsState().log_writer_.open(command.session_id, command.created_utc_ms);
        } else error = gpsState().log_writer_.requestFlush(command.session_id, command.sample_seq,
                                                request.kind == MessageKind::CloseLogSession);
        state_.log = logSnapshot();
        state_.log_result = error;
        ++state_.revision;
        result.status = error == LogError::None ? OperationStatus::Ok : OperationStatus::Unavailable;
    } else if (initialized_ && !hasJob() && !replies_.completionPending() && !work().mapBusy() && (request.kind == MessageKind::SaveSettings || request.kind == MessageKind::OperateStorage)) {
        const auto command = request.payload.storage_command;
        const bool saving = request.kind == MessageKind::SaveSettings &&
                            command.operation == StorageOperation::SaveSettings && validSettings(command.settings);
        const bool sd_operation = request.kind == MessageKind::OperateStorage &&
                                  (command.operation == StorageOperation::Remount || command.operation == StorageOperation::Unmount ||
                                   ((command.operation == StorageOperation::Format || command.operation==StorageOperation::PrepareDirectories) && command.confirmed));
        const bool alarm_operation=!has_read_ && state_.card_state==CardState::Mounted && gpsState().selected_dataset_.ready() &&
            request.kind==MessageKind::OperateStorage && command.settings.dataset==state_.settings.dataset &&
            (command.operation==StorageOperation::SaveAlarm || command.operation==StorageOperation::ReloadAlarm);
        const bool mode_operation = request.kind == MessageKind::OperateStorage && command.confirmed && nvs_ready_ &&
            (command.operation == (StorageOperation::EnterUsbMode) ||
             (command.operation==StorageOperation::EnterWifiMode));
        if (alarm_operation || (saving && nvs_ready_) || ((sd_operation || mode_operation) && !has_read_ && !gpsState().log_writer_.hasRetainedSession())) {
            job_ = command; job_request_id_ = request.request_id;
            job_kind_ = JobKind::Operation;
            state_.busy = true; state_.operation = command.operation;
            if (saving) state_.settings_state = SettingsState::Saving;
            ++state_.revision;
            result.status = OperationStatus::Ok;
        } else if (saving) result.status = OperationStatus::Unavailable;
    }
    state_.log = logSnapshot();
    result.storage = state_;
}

void GpsStorageTask::sendReturn() {
    replies_.sendReturn(results_);
}

void GpsStorageTask::completeDatasetJob() {
    auto& view=*datasetResultMemory(gpsState().dataset_request_);
    view={};view.purpose=gpsState().dataset_request_.purpose;view.id=gpsState().dataset_request_.id;
    const bool listing=gpsState().dataset_request_.action==DatasetAction::Open || gpsState().dataset_request_.action==DatasetAction::Older || gpsState().dataset_request_.action==DatasetAction::Newer;
    const bool maintenance=gpsState().dataset_request_.action==DatasetAction::RebuildIndex || gpsState().dataset_request_.action==DatasetAction::VerifyIndex;
    const bool clearing = gpsState().dataset_request_.action == DatasetAction::Clear;
    const bool applying = clearing || gpsState().dataset_request_.action == DatasetAction::Apply || maintenance;
    gpsState().index_started_at_=taskTimeMs();gpsState().index_progress_={};gpsState().index_progress_.token=gpsState().dataset_request_.token;
    gpsState().index_progress_.verifying=gpsState().dataset_request_.action==DatasetAction::VerifyIndex;
    if (applying && gpsState().log_writer_.hasRetainedSession()) view.error = DatasetError::NotStopped;
    else if (clearing) {
        clearDatasetSelection(view);
    } else if (maintenance && !gpsState().dataset_request_.id) view.error = DatasetError::Unselected;
    else if(listing && gpsState().dataset_request_.purpose==DatasetPurpose::Select && (!mounted_ || state_.card_state!=CardState::Mounted)) view.count=1;
    else if (!mounted_ || state_.card_state != CardState::Mounted) view.error = DatasetError::Missing;
    else if(maintenance) {
        maintainDatasetIndex(view);
    } else {
        if(listing)DatasetCatalog(config::DatasetRoot,this,drive_,&work().files()).readInto(gpsState().dataset_request_,state_.settings.language,view);
        else {
            StorageIndexLease workspace(gpsState().code_images_.map,gpsState().screen_resources_);
            if(!workspace){view.detail=true;view.error=DatasetError::Busy;}
            else {
                DatasetRequest lookup=gpsState().dataset_request_;
                if(lookup.id==gpsState().selected_dataset_.id && !lookup.language[0])
                    std::memcpy(lookup.language,state_.settings.wordbook_language,sizeof(lookup.language));
                DatasetCatalog(config::DatasetRoot,this,drive_,&work().files(),workspace.work()).readInto(lookup,state_.settings.language,view);
            }
        }
    }
    if(state_.card_state!=CardState::Mounted) releaseDataset(PlaceState::NoCard);
    if (applying && !maintenance && !clearing && view.error == DatasetError::None) {
        applyDatasetSelection(view);
    }
    if(applying) {
        gpsState().index_progress_.phase=view.error==DatasetError::None?IndexPhase::Complete:IndexPhase::Failed;
        gpsState().index_progress_.percent=view.error==DatasetError::None?100:gpsState().index_progress_.percent;
        gpsState().index_progress_.elapsed_ms=taskTimeMs()-gpsState().index_started_at_;view.progress=gpsState().index_progress_;
        gpsState().index_progress_dirty_=false;
    }
    view.applying = applying; view.revision = gpsState().dataset_request_.token;
    state_.busy = has_read_; ++state_.revision;
    auto& completion = replies_.beginCompletion(); completion.kind = view.error == DatasetError::None ? MessageKind::ReadCompleted : MessageKind::ReadFailed;
    completion.source = TaskId::Storage; completion.request_id = job_request_id_;
    completion.sequence = ++sequence_; completion.sent_at_ms = taskTimeMs();
    auto& result = completion.payload.storage_completion;
    result.request_id = job_request_id_; result.snapshot = state_;
    result.read_kind = StorageReadKind::DatasetBrowser; result.dataset={gpsState().dataset_request_,view.error};
     job_kind_ = JobKind::None;
    return;
}

void GpsStorageTask::clearDatasetSelection(DatasetView& view) {
    Settings candidate = gpsState().dataset_request_.reset_settings ? defaultSettings() : state_.settings;
    candidate.dataset = 0;
    std::memset(candidate.wordbook_language, 0, sizeof(candidate.wordbook_language));
    const DatasetId none{};
    if (!saveSettings(candidate,&none)) view.error = DatasetError::SaveFailed;
}

void GpsStorageTask::maintainDatasetIndex(DatasetView& view) {
    StorageIndexLease workspace(gpsState().code_images_.map,gpsState().screen_resources_);
    if(!workspace){view.error=DatasetError::Busy;return;}
    const bool active=gpsState().dataset_request_.id==gpsState().selected_dataset_.id;
    if(active)gpsState().place_lookup_.close();
    const auto directory=DatasetCatalog{}.directory(gpsState().dataset_request_.id.view());
    DatasetMetadata metadata;
    readDatasetMetadataInto(directory.view(),state_.settings.language,"",metadata,this);
    view.error=metadata.error;
    if(view.error==DatasetError::None) {
        DatasetIndex index(drive_,this,indexProgress,this,&work().files(),workspace.work());
        view.error=gpsState().dataset_request_.action==DatasetAction::RebuildIndex?
            index.rebuild(directory.view(),metadata):
            index.verifyAgainstDataset(directory.view(),metadata);
    }
    if(active && view.error!=DatasetError::None)releaseDataset(datasetErrorState(view.error));
    Console.printf("[DATASET CACHE] action=%s target=%s result=%u elapsed_ms=%lu\n",
        gpsState().dataset_request_.action==DatasetAction::RebuildIndex?"REBUILD":"VERIFY",gpsState().dataset_request_.id.c_str(),
        static_cast<unsigned>(view.error),static_cast<unsigned long>(taskTimeMs()-gpsState().index_started_at_));
}

void GpsStorageTask::applyDatasetSelection(DatasetView& view) {
    Settings candidate = gpsState().dataset_request_.reset_settings ? defaultSettings() : state_.settings;
    candidate.dataset = 1;
    std::snprintf(candidate.wordbook_language, sizeof(candidate.wordbook_language), "%s", view.info.language);
    const auto directory = DatasetCatalog{}.directory(gpsState().dataset_request_.id.view());
    DatasetMetadata metadata;
    readDatasetMetadataInto(directory.view(),candidate.language,candidate.wordbook_language,metadata,this);
    if (metadata.error != DatasetError::None) view.error = metadata.error;
    else {
        StorageIndexLease workspace(gpsState().code_images_.map,gpsState().screen_resources_);
        if(!workspace){view.error=DatasetError::Busy;return;}
        struct ApplyContext { GpsStorageTask* task; const Settings* settings; const DatasetId* id; } context{this, &candidate,&gpsState().dataset_request_.id};
        const auto index_started = taskTimeMs();
        Console.printf("[DATASET INDEX] generating %s lang=%s\n", gpsState().dataset_request_.id.c_str(), candidate.wordbook_language);
        view.error = DatasetIndex(drive_, this,indexProgress,this,&work().files(),workspace.work()).apply(directory.view(), metadata, [](void* pointer) {
            const auto& apply = *static_cast<ApplyContext*>(pointer);
            return apply.task->saveSettings(*apply.settings,apply.id);
        }, &context);
        Console.printf("[DATASET INDEX] result=%u elapsed_ms=%lu\n", static_cast<unsigned>(view.error),
                       static_cast<unsigned long>(taskTimeMs() - index_started));
    }
}

void GpsStorageTask::completeJob() {
    const bool load_job = job_kind_ == JobKind::LoadSettings;
    const bool search_job = job_kind_ == JobKind::SearchPlace;
    const bool dataset_job = job_kind_ == JobKind::CheckDataset;
    const bool read_job = load_job || search_job || dataset_job;
    if (job_kind_ == JobKind::BrowseDatasets) {
        completeDatasetJob();
        return;
    }
    bool ok = false;
    state_.error = 0;
    PlaceResult local_place{};
    const bool borrowed_place=search_job && validPlaceMemory(gpsState().place_request_);
    PlaceResult& place=borrowed_place?*placeResultMemory(gpsState().place_request_):local_place;
    const uint32_t started_us = config::DetailedDiagnostics ? micros() : 0;
    const auto cache_before = config::DetailedDiagnostics ? gpsState().place_lookup_.cacheStats() : MapCacheStats{};
    const auto wordbook_before = config::DetailedDiagnostics ? gpsState().place_lookup_.wordBookCacheStats() : WordBookCacheStats{};
    if (search_job || dataset_job) {
        if(search_job && gpsState().screen_resources_.retainedMapBytes())reportMapHeap("place-begin-retained",job_request_id_,gpsState().screen_resources_.retainedMapBytes());
        if(search_job && !borrowed_place) {
            place.request=gpsState().place_request_;place.state=PlaceState::DataError;place.dataset=gpsState().place_lookup_.datasetInfo();
        } else if (!state_.settings.dataset || (mounted_ && state_.card_state == CardState::Mounted)) {
            if(dataset_job) {
                StorageIndexLease workspace(gpsState().code_images_.map,gpsState().screen_resources_);
                if(!workspace) {place.state=PlaceState::Waiting;place.dataset=gpsState().place_lookup_.datasetInfo();}
                else {configureDataset();gpsState().place_lookup_.prepareInto(place,workspace.work()->available());}
            }
            else {
                auto& bytes=placeReaderMemory(gpsState().place_request_);
                BorrowedReadBuffer work(bytes.data(),bytes.size());
                if(gpsState().place_request_.alarm_sample)gpsState().place_lookup_.observeAlarmInto(gpsState().place_request_,place,state_.alarm.settings,&work);
                else gpsState().place_lookup_.searchInto(gpsState().place_request_,place,&work,&placeWordBookMemory(gpsState().place_request_));
            }
        } else { place.request = gpsState().place_request_; place.state = PlaceState::NoCard; place.dataset = gpsState().place_lookup_.datasetInfo(); place.dataset.state = PlaceState::NoCard; }
        if(datasetUnavailable(place.state)) {
            releaseDataset(place.state);
            place.dataset=gpsState().place_lookup_.datasetInfo();
        }
        if(search_job && gpsState().screen_resources_.retainedMapBytes())reportMapHeap("place-done-retained",job_request_id_,gpsState().screen_resources_.retainedMapBytes());
        ok = place.state == PlaceState::Unselected || place.state == PlaceState::Ready || place.state == PlaceState::NotFound;
        if (dataset_job && place.state == PlaceState::Ready && !state_.settings.wordbook_language[0])
            std::snprintf(state_.settings.wordbook_language, sizeof(state_.settings.wordbook_language), "%s",
                          gpsState().place_lookup_.selectedLanguage().data());
        if(dataset_job && place.state==PlaceState::Ready)loadAlarmProfile();
    } else if (load_job) ok = loadSettings();
    else switch (job_.operation) {
        case StorageOperation::SaveSettings: ok = saveSettings(job_.settings); break;
        case StorageOperation::SaveAlarm:ok=saveAlarmProfile();break;
        case StorageOperation::ReloadAlarm:loadAlarmProfile();ok=state_.alarm.usable();break;
        case StorageOperation::Remount: mountCard(); ok = state_.card_state == CardState::Mounted; break;
        case StorageOperation::Unmount:
            closeCard(); state_.card_state = CardState::Unmounted;
            state_.card_bytes = state_.total_bytes = state_.free_bytes = 0; state_.fat_type = 0;
            state_.sd_error = 0;
            ok = true; break;
        case StorageOperation::Format: ok = formatCard(); break;
        case StorageOperation::PrepareDirectories: ok = prepareDirectories(); break;
        case StorageOperation::EnterUsbMode: case StorageOperation::EnterWifiMode:
            ok = saveBootMode(job_.operation==StorageOperation::EnterWifiMode?BootMode::WifiManagement:BootMode::UsbStorage);
            if (ok) { closeCard(); state_.card_state = CardState::Unmounted; }
            else state_.nvs_error = state_.error = -21;
            break;
        default: break;
    }
    if(!read_job && (job_.operation==StorageOperation::Remount || job_.operation==StorageOperation::Unmount || job_.operation==StorageOperation::Format) && state_.card_state!=CardState::Mounted) releaseDataset(PlaceState::NoCard);
    state_.busy = has_read_;
    state_.operation = job_.operation;
    state_.operation_ok = ok;
    ++state_.revision;
    auto& completion = replies_.beginCompletion();
    completion.kind = !ok ? (read_job ? MessageKind::ReadFailed : MessageKind::StorageError) :
                       read_job ? MessageKind::ReadCompleted :
                       job_.operation == StorageOperation::SaveSettings ? MessageKind::SettingsWritten :
                       MessageKind::StorageOperationCompleted;
    completion.source = TaskId::Storage;
    completion.sequence = ++sequence_;
    completion.sent_at_ms = taskTimeMs();
    completion.request_id = job_request_id_;
    auto& result = completion.payload.storage_completion;
    result = {};
    result.request_id = job_request_id_;
    result.operation_id = job_.operation_id;
    state_.log = logSnapshot();
    result.snapshot = state_;
    result.read_kind = dataset_job ? StorageReadKind::Dataset : search_job ? StorageReadKind::Place :
                       load_job ? StorageReadKind::Settings : StorageReadKind::None;
    // Readers/bindings have ended. Completion publishes ownership and metadata;
    // the text remains exclusively in the UI loan until App commits or discards.
    result.place=placeCompletion(place);

    job_kind_ = JobKind::None;
    if constexpr (config::DetailedDiagnostics) {
        if (search_job || dataset_job) {
            const auto cache = gpsState().place_lookup_.cacheStats();
            const auto wordbook = gpsState().place_lookup_.wordBookCacheStats();
            Console.printf("[DATASET] chunks=%llu max=%lu state=%s\n",
                          static_cast<unsigned long long>(place.dataset.chunks), static_cast<unsigned long>(place.dataset.limit),
                          placeStateName(place.state));
            Console.printf("[GIS] %s code=%lld detail=%s stack_free=%u heap=%u min_heap=%u largest=%u\n",
                                      placeStateName(place.state), static_cast<long long>(place.region_code), place.detail,
                                      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), ESP.getFreeHeap(),
                                      ESP.getMinFreeHeap(), static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
            if (search_job) Console.printf("[CACHE] %s bytes=%u hit=%lu miss=%lu fill=%lu valid=%u rect=%d,%d,%d,%d elapsed_us=%lu\n",
                                          cache.valid ? (cache.hits > cache_before.hits ? "HIT" : "MISS") : "INVALID",
                                          static_cast<unsigned>(cache.bytes), static_cast<unsigned long>(cache.hits),
                                          static_cast<unsigned long>(cache.misses), static_cast<unsigned long>(cache.fills),
                                          cache.valid, cache.bounds.x, cache.bounds.y, CachedMapReader::Side, CachedMapReader::Side,
                                          static_cast<unsigned long>(micros() - started_us));
            if (search_job) Console.printf("[WORDBOOK] %s entries=%u/%u hit=%lu miss=%lu\n",
                                          place.state != PlaceState::Ready ? "NONE" :
                                          wordbook.hits != wordbook_before.hits ? "HIT" : "MISS",
                                          static_cast<unsigned>(wordbook.entries), static_cast<unsigned>(WordBookCache::Capacity),
                                          static_cast<unsigned long>(wordbook.hits),
                                          static_cast<unsigned long>(wordbook.misses));
            if (search_job && mounted_ && state_.card_state == CardState::Mounted) {
                const auto& time = gpsState().place_lookup_.readTiming();
                Console.printf("[READ-TIME] total_us=%llu open_us=%llu map_us=%llu rect_us=%llu wordbook_us=%llu text_us=%llu\n",
                              static_cast<unsigned long long>(time.total_us), static_cast<unsigned long long>(time.open_us),
                              static_cast<unsigned long long>(time.map_us), static_cast<unsigned long long>(time.rect_us),
                              static_cast<unsigned long long>(time.wordbook_us), static_cast<unsigned long long>(time.text_us));
                Console.printf("[READ-IO] map_us=%llu map_bytes=%llu map_reads=%lu map_seeks=%lu map_opens=%lu map_closes=%lu book_us=%llu book_bytes=%llu book_reads=%lu book_seeks=%lu book_opens=%lu book_closes=%lu\n",
                              static_cast<unsigned long long>(time.map_io.elapsed_us), static_cast<unsigned long long>(time.map_io.bytes),
                              static_cast<unsigned long>(time.map_io.reads), static_cast<unsigned long>(time.map_io.seeks),
                              static_cast<unsigned long>(time.map_io.opens), static_cast<unsigned long>(time.map_io.closes),
                              static_cast<unsigned long long>(time.wordbook_io.elapsed_us), static_cast<unsigned long long>(time.wordbook_io.bytes),
                              static_cast<unsigned long>(time.wordbook_io.reads), static_cast<unsigned long>(time.wordbook_io.seeks),
                              static_cast<unsigned long>(time.wordbook_io.opens), static_cast<unsigned long>(time.wordbook_io.closes));
                auto print_io_time = [](const char* name, const FileIoTiming& io) {
                    Console.printf("[%s-IO-TIME] read_us=%llu seek_us=%llu open_us=%llu close_us=%llu stat_us=%llu\n", name,
                                  static_cast<unsigned long long>(io.read_us), static_cast<unsigned long long>(io.seek_us),
                                  static_cast<unsigned long long>(io.open_us), static_cast<unsigned long long>(io.close_us),
                                  static_cast<unsigned long long>(io.stat_us));
                };
                print_io_time("MAP", time.map_io);
                print_io_time("BOOK", time.wordbook_io);
                print_io_time("INDEX", time.index_io);
                Console.printf("[INDEX-IO] bytes=%llu reads=%lu seeks=%lu opens=%lu closes=%lu\n",
                    static_cast<unsigned long long>(time.index_io.bytes),static_cast<unsigned long>(time.index_io.reads),
                    static_cast<unsigned long>(time.index_io.seeks),static_cast<unsigned long>(time.index_io.opens),static_cast<unsigned long>(time.index_io.closes));
            }
        } else Console.printf("[STORAGE] op=%u ok=%u NVS=%u SD=%s error=%ld stack_free=%u\n",
                      static_cast<unsigned>(state_.operation), ok, static_cast<unsigned>(state_.settings_state),
                      cardStateName(state_.card_state), static_cast<long>(state_.error),
                      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    } else if ((search_job || dataset_job) && datasetUnavailable(place.state)) {
        Console.printf("[GIS] ERROR %s: %s\n",placeStateName(place.state),place.detail);
    } else if (!read_job && !ok) {
        Console.printf("[STORAGE] ERROR op=%u code=%ld\n",static_cast<unsigned>(state_.operation),static_cast<long>(state_.error));
    }
}

void GpsStorageTask::notify() {
    replies_.notifyCompletion(notifications_);
}

void GpsStorageTask::sdBegin() {
    if (sd_depth_++) return;
    ++state_.sd.access_id;
    if (!++state_.sd.revision) ++state_.sd.revision;
    state_.sd.active = true; sd_dirty_ = true;
    notifyActivity();
}
void GpsStorageTask::sdEnd() {
    if (!sd_depth_ || --sd_depth_) return;
    if (!++state_.sd.revision) ++state_.sd.revision;
    state_.sd.active = false; sd_dirty_ = true;
    notifyActivity();
}
void GpsStorageTask::cacheState(RgCacheState cache, bool new_dataset) {
    if (!new_dataset && cache == state_.rg.state) return;
    if (new_dataset) ++state_.rg.dataset_generation;
    state_.rg.state = cache;
    if (!++state_.rg.revision) ++state_.rg.revision;
    rg_dirty_ = true;
    notifyActivity();
}
void GpsStorageTask::indexProgress(void* pointer,const IndexProgress& progress) {
    auto& self=*static_cast<GpsStorageTask*>(pointer);self.gpsState().index_progress_=progress;
    self.gpsState().index_progress_.token=self.gpsState().dataset_request_.token;
    self.gpsState().index_progress_.elapsed_ms=taskTimeMs()-self.gpsState().index_started_at_;
    self.gpsState().index_progress_dirty_=true;self.notifyActivity();
}
void GpsStorageTask::notifyActivity() {
    if(gpsState().index_progress_dirty_ && taskTimeMs()-gpsState().index_progress_at_>=200) {
        AppNotification progress{};progress.kind=MessageKind::DatasetProgress;progress.source=TaskId::Storage;
        progress.payload.index_progress=gpsState().index_progress_;
        if(sendSpeculative(notifications_,progress)==SpeculativeResult::Queued) {gpsState().index_progress_dirty_=false;gpsState().index_progress_at_=taskTimeMs();}
    }
    AppNotification event{};
    event.source = TaskId::Storage; event.sent_at_ms = taskTimeMs();
    if (rg_dirty_) {
        event.kind = MessageKind::RgCacheStateChanged;
        event.payload.rg = state_.rg;
        if (sendSpeculative(notifications_, event) == SpeculativeResult::Queued) rg_dirty_ = false;
    }
    if (sd_dirty_) {
        event.kind = MessageKind::SdAccessChanged;
        event.payload.sd = state_.sd;
        if (sendSpeculative(notifications_, event) == SpeculativeResult::Queued) sd_dirty_ = false;
    }
}
void GpsStorageTask::flushLog(bool force) {
    const auto log = logSnapshot();
    const auto now = taskTimeMs();  // Sample time after the producer-published timestamp.
    const bool due = log.buffered && now - log.buffer_since_ms >= FlushIntervals[state_.settings.flush_interval] * 1000u;
    if ((!force && !gpsState().log_writer_.needsFlush() && !due) || static_cast<int32_t>(now - gpsState().next_log_retry_) < 0) return;
    const bool ok = gpsState().log_writer_.flush(now);
    state_.log = logSnapshot();
    state_.log_result = ok ? LogError::None : state_.log.error;
    gpsState().next_log_retry_ = ok ? now : now + 5000;
    ++state_.revision;
    auto& completion = replies_.beginCompletion();
    completion.kind = ok ? MessageKind::LogWritten : MessageKind::StorageError;
    completion.source = TaskId::Storage;
    completion.sent_at_ms = now;
    completion.payload.storage_completion.snapshot = state_;
    completion.payload.storage_completion.read_kind = StorageReadKind::Log;

    Console.printf("[LOG] saved=%lu buffered=%u closing=%u error=%u\n",
                  static_cast<unsigned long>(state_.log.saved), state_.log.buffered,
                  state_.log.closing, static_cast<unsigned>(state_.log.error));
}

void GpsStorageTask::stepLogRead() {
    if(!read_prepared_ && !work().logs().done()) {
        const auto log=logSnapshot();
        if(state_.card_state!=CardState::Mounted) work().logs().fail(LogReadError::NoCard);
        else if(work().logs().result().request.kind!=LogReadKind::Delete && log.session_id==read_barrier_session_ && log.saved<read_barrier_) {
            if(log.error!=LogError::None && static_cast<int32_t>(taskTimeMs()-gpsState().next_log_retry_)<0)
                work().logs().fail(LogReadError::SaveFailed);
            else { flushLog(true); return; }
        } else read_prepared_=true;
    }
    auto& reader=work().logs();
    const bool statistics=reader.result().request.kind==LogReadKind::Statistics;
    const auto start=monotonicMicros();
    for(unsigned n=0;!reader.done();++n) {
        reader.step(&gpsState().log_writer_);
        if(!statistics || n+1>=config::LogStatisticsStepsPerLoop || monotonicMicros()-start>=config::LogStatisticsBudgetUs)break;
    }
    if(!reader.done()) {
        if(statistics && taskTimeMs()-statistics_progress_at_>=config::LogStatisticsProgressMs) {
            AppNotification progress{};progress.source=TaskId::Storage;progress.kind=MessageKind::LogStatisticsProgress;
            progress.payload.log_progress=reader.progress();
            if(sendSpeculative(notifications_,progress)==SpeculativeResult::Queued)statistics_progress_at_=taskTimeMs();
        }
        return;
    }
    has_read_=false; state_.busy=false; ++state_.revision;
    state_.log=logSnapshot();
    auto& completion = replies_.beginCompletion(); completion.source=TaskId::Storage; completion.request_id=read_request_id_;
    completion.sequence=++sequence_; completion.sent_at_ms=taskTimeMs();
    const auto& read=work().logs().result();
    completion.kind=read.error==LogReadError::None?MessageKind::ReadCompleted:MessageKind::ReadFailed;
    auto& result=completion.payload.storage_completion;
    result.request_id=read_request_id_; result.snapshot=state_; result.read_kind=StorageReadKind::LogBrowse;
    result.logs=read;
    if constexpr (config::DetailedDiagnostics) Console.printf("[BROWSE] kind=%u token=%lu error=%u count=%u records=%lu start=%lu stack_free=%u\n",
                  static_cast<unsigned>(read.request.kind),static_cast<unsigned long>(read.request.token),static_cast<unsigned>(read.error),
                  read.count,static_cast<unsigned long>(read.summary.records),static_cast<unsigned long>(read.request.start),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void GpsStorageTask::stepMap() {
    auto* map=work().activeMap();
    if(!map)return;
    if(map->reading()) {
        const auto request=map->info().request;
        const auto started=config::DetailedDiagnostics ? taskTimeMs() : 0;
        reportMapHeap("read-begin",request.token);
        map->loaded(readBoundaryMap(gpsState().selected_dataset_,request,drive_,this,
            std::move(gpsState().screen_resources_.session(request.session)),map->readBuffer(),map->readFile()));
        if constexpr (config::DetailedDiagnostics) Console.printf("[MAP] read token=%lu error=%u elapsed_ms=%lu heap=%u\n",
            static_cast<unsigned long>(request.token),static_cast<unsigned>(map->info().error),
            static_cast<unsigned long>(taskTimeMs()-started),ESP.getFreeHeap());
        reportMapHeap("render-begin",request.token);
    } else {
        const bool was_complete=map->complete();
        map->step();
        if(!was_complete && map->complete())reportMapHeap("render-done-retained",map->info().request.token,
            gpsState().screen_resources_.session(map->info().request.session).bytes());
    }
    if(map->notificationPending()) {
        AppNotification event{};event.kind=MessageKind::MapCompleted;event.source=TaskId::Storage;
        event.request_id=map->info().request.token;event.payload.map=map->info();
        if(sendSpeculative(notifications_,event)==SpeculativeResult::Queued)map->notificationSent();
    }
}

void GpsStorageTask::initializeCardHardware() {initializeCardSpi(spi_);}

void GpsStorageTask::run() {

    for (;;) {
        sendReturn();
        notify();
        StorageRequest request{};
        // Keep the one accepted operation/result until delivered. An incoming
        // read notification must never be consumed and silently discarded.
        if (!replies_.returnPending() && !hasJob() && !replies_.completionPending() && inbox().receive(request)) { handle(request); sendReturn(); }
        if(browse_release_generation_ && !has_read_ && !hasJob() && !replies_.returnPending() && !replies_.completionPending() && work().releaseLogs(browse_release_generation_))browse_release_generation_=0;
        // Display notifications never gate physical work. Only accepted job/results are retained.
        if (!replies_.returnPending() && hasJob()) completeJob();
        if (!replies_.returnPending() && !hasJob() && !replies_.completionPending()) flushLog();
        if (!replies_.returnPending() && !hasJob() && !replies_.completionPending() && has_read_) {

            stepLogRead();
        }
        if(!replies_.returnPending() && !hasJob() && !replies_.completionPending() && !has_read_) stepMap();

        notifyActivity();
        notify();
        const bool statistics=has_read_ && work().logs().result().request.kind==LogReadKind::Statistics;
        vTaskDelay(pdMS_TO_TICKS(statistics?1:5));
    }
}

}  // namespace geoputer

namespace geoputer {
void GpsStorageTask::loadAlarmProfile() {
    const auto revision=state_.alarm.revision+1;
    state_.alarm={};state_.alarm.revision=revision;
    if(!nvs_ready_ || state_.nvs_error || state_.card_state!=CardState::Mounted || !gpsState().selected_dataset_.ready())return;
    using namespace index_format;
    uint32_t identity=2166136261u;
    for(auto b:gpsState().selected_dataset_.map_index.header)identity=(identity^b)*16777619u;
    for(auto b:gpsState().selected_dataset_.book_index.header)identity=(identity^b)*16777619u;
    const auto depth=get16(gpsState().selected_dataset_.book_index.header.data()+Wordbook::Depth);
    state_.alarm=AlarmFile(drive_,work().files().primary(),this).load(gpsState().selected_dataset_.id.view(),depth,identity);
    state_.alarm.revision=revision;
    Console.printf("[ALARM] dataset=%s depth=%u state=%s enabled=%u\n",gpsState().selected_dataset_.id.c_str(),state_.alarm.depth,alarm::stateName(state_.alarm.state),state_.alarm.settings.enabled);
}
bool GpsStorageTask::saveAlarmProfile() {
    if(state_.card_state!=CardState::Mounted || !state_.alarm.usable() || job_.alarm_identity!=state_.alarm.identity || job_.settings.dataset!=state_.settings.dataset || !alarm::valid(job_.alarm_settings,state_.alarm.depth))return false;
    auto next=state_.alarm;next.settings=job_.alarm_settings;
    if(!AlarmFile(drive_,work().files().primary(),this).save(next,gpsState().selected_dataset_.id.view())) {
        state_.alarm.settings.enabled=false;state_.alarm.state=alarm::State::IoError;++state_.alarm.revision;return false;
    }
    next.state=alarm::State::Ready;++next.revision;state_.alarm=next;return true;
}
} // namespace geoputer

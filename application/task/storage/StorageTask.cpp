#include "../../utils/Console.h"
#include "StorageTask.h"
#include "../../utils/HeapDiagnostics.h"
#include "DatasetSelection.h"
#include "dataset/DatasetIndex.h"
#include <SD.h>
#include <sd_diskio.h>
#include <ff.h>
#include <esp_heap_caps.h>
#include <cstring>
#include "../../utils/SyncClient.h"
#include "../../BootMode.h"
#include "../../hardware/LoraSafety.h"

namespace geoputer {

LogError StorageTask::tryAppendLog(const LogCommand& command, LogSnapshot& snapshot) {
    const auto error = log_writer_.append(command, taskTimeMs());
    snapshot = log_writer_.snapshot();
    return error;
}

void StorageTask::closeCard() {
    SdAccessScope access(this);
    screen_resources_.map.release(screen_resources_.map.info().request);
    place_lookup_.close();
    if (drive_ == 0xFF) return;
    sdcard_unmount(drive_);
    sdcard_uninit(drive_);
    drive_ = 0xFF;
    mounted_ = false;
}

void StorageTask::mountCard() {
    SdAccessScope access(this);
    closeCard();
    state_.card_state = CardState::NoCard;
    state_.card_bytes = state_.total_bytes = state_.free_bytes = 0;
    state_.fat_type = 0;
    drive_ = sdcard_init(12, &spi_, config::SdSpiHz);
    if (drive_ == 0xFF) { state_.sd_error = state_.error = -10; return; }
    place_lookup_.setDrive(drive_);
    log_writer_.setDrive(drive_);
    screen_resources_.logs.setDrive(drive_);
    mounted_ = sdcard_mount(drive_, "/sd", config::SdMaxOpenFiles, false);  // Never format on mount failure.
    const auto type = sdcard_type(drive_);
    if (type == CARD_SD || type == CARD_SDHC || type == CARD_MMC) {
        state_.card_bytes = static_cast<uint64_t>(sdcard_num_sectors(drive_)) * 512;
        state_.card_state = CardState::Unsupported;
    }
    if (!mounted_) { state_.sd_error = state_.error = -11; return; }
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
}

bool StorageTask::formatCard() {
    SdAccessScope access(this);
    // Only reached via a confirmed command. No automatic formatting or NVS erasure.
    closeCard();
    drive_ = sdcard_init(12, &spi_, config::SdSpiHz);
    if (drive_ == 0xFF) { state_.sd_error = state_.error = -10; return false; }
    char volume[] = {static_cast<char>('0' + drive_), ':', '\0'};
    uint8_t work[4096];
    const auto result = f_mkfs(volume, FM_FAT32 | FM_SFD, 0, work, sizeof(work));
    closeCard();
    if (result != FR_OK) {
        state_.card_state = CardState::Failed;
        state_.total_bytes = state_.free_bytes = 0;
        state_.fat_type = 0;
        state_.sd_error = state_.error = static_cast<int32_t>(result);
        return false;
    }
    mountCard();
    return state_.card_state == CardState::Mounted;
}

void StorageTask::configureDataset() {
    place_lookup_.configure(state_.settings.dataset ? DatasetCatalog{}.directory(state_.settings.dataset) : std::string{},
                            state_.settings.wordbook_language);
}

void StorageTask::releaseDataset(PlaceState reason) {
    if (!releaseUnusableDataset(settings_store_, state_, reason)) return;
    configureDataset();
    Console.printf("[DATASET RELEASE] number=%02u reason=%s saved=%u\n",state_.dataset_failure.number,
                   placeStateName(reason),!state_.dataset_failure.save_failed);
}

bool StorageTask::loadSettings() {
    state_.settings_state = settings_store_.load(state_.settings);
    configureDataset();
    if (state_.settings_state != SettingsState::Loaded && state_.settings_state != SettingsState::Defaults) {
        state_.nvs_error = state_.error = -20;
        return false;
    }
    state_.nvs_error = 0;
    return true;
}

bool StorageTask::saveSettings(const Settings& value) {
    // One versioned, checked record; never clear the namespace or the NVS partition.
    if (!settings_store_.save(value)) {
        state_.settings_state = SettingsState::Failed;
        state_.nvs_error = state_.error = -21;
        return false;
    }
    const bool selection_changed = state_.settings.dataset != value.dataset ||
        std::strcmp(state_.settings.wordbook_language, value.wordbook_language);
    state_.settings = value;
    state_.dataset_failure.save_failed = false;
    if (selection_changed) configureDataset();
    state_.settings_state = SettingsState::Saved;
    state_.nvs_error = 0;
    return true;
}

void StorageTask::handle(const StorageRequest& request) {
    if (request.source != TaskId::Application || request.request_id == 0) return;
    if(request.kind==MessageKind::CancelLogRead) {
        if(has_read_ && screen_resources_.logs.result().request.kind!=LogReadKind::Delete &&
           request.payload.log_read.token==screen_resources_.logs.result().request.token) screen_resources_.logs.cancel();
        return;
    }
    if (request.kind == MessageKind::LoadSettings || request.kind == MessageKind::SearchPlace ||
        request.kind == MessageKind::CheckDataset) {
        if (!hasJob() && !has_completion_ && !screen_resources_.map.occupied()) {
            job_kind_ = request.kind == MessageKind::LoadSettings ? JobKind::LoadSettings :
                        request.kind == MessageKind::SearchPlace ? JobKind::SearchPlace : JobKind::CheckDataset;
            place_request_ = request.kind == MessageKind::SearchPlace ? request.payload.place_request : PlaceRequest{};
            job_ = {}; job_request_id_ = request.request_id;
            state_.busy = true;
        }
        return;
    }
    prepareSyncReturn(pending_return_, TaskId::Storage, request, ++sequence_);
    auto& result = pending_return_.payload.result;
    if(request.kind==MessageKind::RequestMap) {
        if(!usb_mode_ && initialized_ && !hasJob() && !has_completion_ && !has_read_ && !screen_resources_.map.occupied()) {
            const auto& command=request.payload.map;
            if(screen_resources_.map.start(command)) {
                if(state_.card_state!=CardState::Mounted)screen_resources_.map.fail(map::Error::NoCard);
                else if(!state_.settings.dataset)screen_resources_.map.fail(map::Error::NoDataset);
                else if(command.dataset_generation!=place_lookup_.datasetInfo().generation)screen_resources_.map.fail(map::Error::Stale);
                result.status=OperationStatus::Ok;
            }
        }
    } else if(request.kind==MessageKind::ReadMapRows) {
        if(screen_resources_.map.read(request.payload.map,result.map))result.status=OperationStatus::Ok;
    } else if(request.kind==MessageKind::ReleaseMap || request.kind==MessageKind::CancelMap) {
        if(screen_resources_.map.release(request.payload.map)) {
            result.status=OperationStatus::Ok;
            reportMapHeap(request.kind==MessageKind::ReleaseMap?"released":"cancelled",request.payload.map.token);
        }
    } else if(request.kind==MessageKind::UsbExportOperation) {
        handleUsbExport(request,result);
    } else if (request.kind == MessageKind::DatasetOperation) {
        if (!usb_mode_ && initialized_ && !has_read_ && !hasJob() && !has_completion_ && !screen_resources_.map.occupied()) {
            dataset_request_ = request.payload.dataset;
            dataset_request_.language[sizeof(dataset_request_.language)-1] = 0;
            job_kind_ = JobKind::BrowseDatasets;
            job_ = {}; job_request_id_ = request.request_id;
            state_.busy = true; ++state_.revision; result.status = OperationStatus::Ok;
        } else result.status = OperationStatus::Unavailable;
    } else if (request.kind == MessageKind::InitializeStorage) {
        if (!initialized_) {
            digitalWrite(config::LoraSelectPin, HIGH);  // Keep the radio deselected until the controlled SLEEP transition.
            pinMode(config::LoraSelectPin, OUTPUT);
            pinMode(12, OUTPUT); digitalWrite(12, HIGH);
            spi_.begin(40, 39, 14, 12);
            uint8_t lora_status = 0xff;
            const bool lora_sleep = sleepUnusedLora(spi_, &lora_status);
            Console.printf("[LORA] %s status=0x%02X (TX/RX disabled)\n", lora_sleep ? "SLEEP" : "RESET HELD", lora_status);
            nvs_ready_ = settings_store_.begin();
            state_.settings = defaultSettings();
            configureDataset();
            state_.settings_state = nvs_ready_ ? SettingsState::Defaults : SettingsState::Unavailable;
            state_.nvs_error = nvs_ready_ ? 0 : -20;
            if (usb_mode_) {
                state_.settings_state = settings_store_.load(state_.settings);
                usb_.begin(spi_);
                state_.usb = usb_.snapshot(); usb_dirty_ = true;
                state_.card_state = CardState::Unmounted; // No FatFs mount while the card is published to the PC.
                Console.printf("[USB] raw SD ready=%u; FATFS deferred; GPS/GIS disabled\n",state_.usb.ready);
            } else mountCard();
            state_.revision = 1;
            initialized_ = true;
            Console.printf("[STORAGE] NVS=%s SD=%s spi_hz=%lu spi_actual_hz=%lu\n", nvs_ready_ ? "ready" : "error", cardStateName(state_.card_state), static_cast<unsigned long>(config::SdSpiHz), static_cast<unsigned long>(spiClockDivToFrequency(spi_.getClockDivider())));
        }
        result.status = OperationStatus::Ok;  // Individual backend outcomes are in the snapshot.
    } else if (request.kind == MessageKind::GetStorageStatus) {
        result.status = initialized_ ? OperationStatus::Ok : OperationStatus::Unavailable;
    } else if(request.kind==MessageKind::ListLogs || request.kind==MessageKind::ReadLogSummary || request.kind==MessageKind::ReadLogRecords || request.kind==MessageKind::DeleteLog) {
        if(initialized_ && !has_read_ && !hasJob() && !has_completion_ && !screen_resources_.map.occupied() &&
           (!usb_mode_ || (usb_.local() && state_.card_state==CardState::Mounted &&
                           state_.export_job.phase==ExportPhase::Selecting && request.kind==MessageKind::ListLogs))) {
            auto read=request.payload.log_read;
            read.kind=request.kind==MessageKind::ListLogs?LogReadKind::List:request.kind==MessageKind::ReadLogSummary?LogReadKind::Summary:request.kind==MessageKind::DeleteLog?LogReadKind::Delete:LogReadKind::Records;
            if(screen_resources_.logs.begin(read)) {
                has_read_=true; read_prepared_=false; read_request_id_=request.request_id;
                const auto log=log_writer_.snapshot(); read_barrier_session_=log.session_id; read_barrier_=log.accepted;
                state_.busy=true; ++state_.revision; result.status=OperationStatus::Ok;
            }
        } else result.status=OperationStatus::Unavailable;
    } else if (initialized_ && (request.kind == MessageKind::OpenLogSession ||
               request.kind == MessageKind::FlushLog ||
               request.kind == MessageKind::CloseLogSession)) {
        const auto& command = request.payload.log;
        LogError error = LogError::Invalid;
        if (state_.card_state != CardState::Mounted) error = LogError::Io;
        else if (request.kind == MessageKind::OpenLogSession) {
            error = log_writer_.open(command.session_id, command.created_utc_ms);
        } else error = log_writer_.requestFlush(command.session_id, command.sample_seq,
                                                request.kind == MessageKind::CloseLogSession);
        state_.log = log_writer_.snapshot();
        state_.log_result = error;
        ++state_.revision;
        result.status = error == LogError::None ? OperationStatus::Ok : OperationStatus::Unavailable;
    } else if (initialized_ && !hasJob() && !has_completion_ && !screen_resources_.map.occupied() &&
               (request.kind == MessageKind::SaveSettings || request.kind == MessageKind::OperateStorage)) {
        const auto command = request.payload.storage_command;
        const bool saving = request.kind == MessageKind::SaveSettings &&
                            command.operation == StorageOperation::SaveSettings && validSettings(command.settings);
        const bool sd_operation = request.kind == MessageKind::OperateStorage &&
                                  (command.operation == StorageOperation::Remount || command.operation == StorageOperation::Unmount ||
                                   (command.operation == StorageOperation::Format && command.confirmed));
        const bool mode_operation = request.kind == MessageKind::OperateStorage && command.confirmed && nvs_ready_ &&
            (command.operation == (usb_mode_ ? StorageOperation::ExitUsbMode : StorageOperation::EnterUsbMode));
        if ((!usb_mode_ && saving && nvs_ready_) ||
            (((!usb_mode_ && sd_operation) || mode_operation) && !has_read_ && !log_writer_.hasRetainedSession() &&
              (!usb_mode_ || (usb_work_==UsbWork::None && state_.export_job.phase==ExportPhase::Idle)))) {
            job_ = command; job_request_id_ = request.request_id;
            job_kind_ = JobKind::Operation;
            state_.busy = true; state_.operation = command.operation;
            if (saving) state_.settings_state = SettingsState::Saving;
            ++state_.revision;
            result.status = OperationStatus::Ok;
        } else if (saving) result.status = OperationStatus::Unavailable;
    }
    state_.log = log_writer_.snapshot();
    if (request.kind != MessageKind::ReadMapRows) result.storage = state_;
    has_return_ = true;
}

void StorageTask::sendReturn() {
    if (has_return_ && sendSpeculative(results_, pending_return_) == SpeculativeResult::Queued) has_return_ = false;
}

void StorageTask::completeDatasetJob() {
    DatasetView view{};view.purpose=dataset_request_.purpose;view.number=dataset_request_.number;
    const bool listing=dataset_request_.action==DatasetAction::Open || dataset_request_.action==DatasetAction::Older || dataset_request_.action==DatasetAction::Newer;
    const bool maintenance=dataset_request_.action==DatasetAction::RebuildIndex || dataset_request_.action==DatasetAction::VerifyIndex;
    const bool clearing = dataset_request_.action == DatasetAction::Clear;
    const bool applying = clearing || dataset_request_.action == DatasetAction::Apply || maintenance;
    index_started_at_=taskTimeMs();index_progress_={};index_progress_.token=dataset_request_.token;
    index_progress_.verifying=dataset_request_.action==DatasetAction::VerifyIndex;
    if (applying && log_writer_.hasRetainedSession()) view.error = DatasetError::NotStopped;
    else if (clearing) {
        clearDatasetSelection(view);
    } else if (maintenance && (!dataset_request_.number || dataset_request_.number>99)) view.error = DatasetError::Unselected;
    else if(listing && dataset_request_.purpose==DatasetPurpose::Select && (!mounted_ || state_.card_state!=CardState::Mounted)) view.count=1;
    else if (!mounted_ || state_.card_state != CardState::Mounted) view.error = DatasetError::Missing;
    else if(maintenance) {
        maintainDatasetIndex(view);
    } else {
        view = DatasetCatalog("/sd/dataset", this, drive_).read(dataset_request_, state_.settings.language);
    }
    if(!usb_mode_ && state_.card_state!=CardState::Mounted) releaseDataset(PlaceState::NoCard);
    if (applying && !maintenance && !clearing && view.error == DatasetError::None) {
        applyDatasetSelection(view);
    }
    if(applying) {
        index_progress_.phase=view.error==DatasetError::None?IndexPhase::Complete:IndexPhase::Failed;
        index_progress_.percent=view.error==DatasetError::None?100:index_progress_.percent;
        index_progress_.elapsed_ms=taskTimeMs()-index_started_at_;view.progress=index_progress_;
        index_progress_dirty_=false;
    }
    view.applying = applying; view.revision = dataset_request_.token;
    state_.busy = has_read_; ++state_.revision;
    completion_ = {}; completion_.kind = view.error == DatasetError::None ? MessageKind::ReadCompleted : MessageKind::ReadFailed;
    completion_.source = TaskId::Storage; completion_.request_id = job_request_id_;
    completion_.sequence = ++sequence_; completion_.sent_at_ms = taskTimeMs();
    auto& result = completion_.payload.storage_completion;
    result.request_id = job_request_id_; result.snapshot = state_;
    result.read_kind = StorageReadKind::DatasetBrowser; result.dataset_view = view;
    has_completion_ = true; job_kind_ = JobKind::None;
    return;
}

void StorageTask::clearDatasetSelection(DatasetView& view) {
    Settings candidate = dataset_request_.reset_settings ? defaultSettings() : state_.settings;
    candidate.dataset = 0;
    std::memset(candidate.wordbook_language, 0, sizeof(candidate.wordbook_language));
    if (!saveSettings(candidate)) view.error = DatasetError::SaveFailed;
}

void StorageTask::maintainDatasetIndex(DatasetView& view) {
    const bool active=dataset_request_.number==state_.settings.dataset;
    if(active)place_lookup_.close();
    const auto directory=DatasetCatalog{}.directory(dataset_request_.number);
    const auto metadata=readDatasetMetadata(directory,state_.settings.language,"",this);
    view.error=metadata.error;
    if(view.error==DatasetError::None) {
        DatasetIndex index(drive_,this,indexProgress,this);
        view.error=dataset_request_.action==DatasetAction::RebuildIndex?
            index.rebuild(directory,metadata,dataset_request_.index_target):
            index.verifyAgainstDataset(directory,metadata,dataset_request_.index_target);
    }
    if(active && view.error!=DatasetError::None)releaseDataset(datasetErrorState(view.error));
    Console.printf("[DATASET CACHE] action=%s target=%02u result=%u elapsed_ms=%lu\n",
        dataset_request_.action==DatasetAction::RebuildIndex?"REBUILD":"VERIFY",dataset_request_.number,
        static_cast<unsigned>(view.error),static_cast<unsigned long>(taskTimeMs()-index_started_at_));
}

void StorageTask::applyDatasetSelection(DatasetView& view) {
    Settings candidate = dataset_request_.reset_settings ? defaultSettings() : state_.settings;
    candidate.dataset = dataset_request_.number;
    std::snprintf(candidate.wordbook_language, sizeof(candidate.wordbook_language), "%s", view.info.language);
    const auto directory = DatasetCatalog{}.directory(candidate.dataset);
    const auto metadata = readDatasetMetadata(directory, candidate.language, candidate.wordbook_language, this);
    if (metadata.error != DatasetError::None) view.error = metadata.error;
    else {
        struct ApplyContext { StorageTask* task; const Settings* settings; } context{this, &candidate};
        const auto index_started = taskTimeMs();
        Console.printf("[DATASET INDEX] generating %02u lang=%s\n", candidate.dataset, candidate.wordbook_language);
        view.error = DatasetIndex(drive_, this,indexProgress,this).apply(directory, metadata, [](void* pointer) {
            const auto& apply = *static_cast<ApplyContext*>(pointer);
            return apply.task->saveSettings(*apply.settings);
        }, &context);
        Console.printf("[DATASET INDEX] result=%u elapsed_ms=%lu\n", static_cast<unsigned>(view.error),
                       static_cast<unsigned long>(taskTimeMs() - index_started));
    }
    if (view.error == DatasetError::None)
        place_lookup_.configure(DatasetCatalog{}.directory(candidate.dataset), candidate.wordbook_language);
}

void StorageTask::completeJob() {
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
    PlaceResult place{};
    const uint32_t started_us = config::DetailedDiagnostics ? micros() : 0;
    const auto cache_before = config::DetailedDiagnostics ? place_lookup_.cacheStats() : MapCacheStats{};
    const auto wordbook_before = config::DetailedDiagnostics ? place_lookup_.wordBookCacheStats() : WordBookCacheStats{};
    if (search_job || dataset_job) {
        if (dataset_job) configureDataset();
        if (!state_.settings.dataset || (mounted_ && state_.card_state == CardState::Mounted))
            place = dataset_job ? place_lookup_.prepare() : place_lookup_.search(place_request_);
        else { place.request = place_request_; place.state = PlaceState::NoCard; place.dataset = place_lookup_.datasetInfo(); place.dataset.state = PlaceState::NoCard; }
        if(datasetUnavailable(place.state)) {
            releaseDataset(place.state);
            place.dataset=place_lookup_.datasetInfo();
        }
        ok = place.state == PlaceState::Unselected || place.state == PlaceState::Ready || place.state == PlaceState::NotFound;
        if (dataset_job && place.state == PlaceState::Ready && !state_.settings.wordbook_language[0])
            std::snprintf(state_.settings.wordbook_language, sizeof(state_.settings.wordbook_language), "%s",
                          place_lookup_.selectedLanguage().c_str());
    } else if (load_job) ok = loadSettings();
    else switch (job_.operation) {
        case StorageOperation::SaveSettings: ok = saveSettings(job_.settings); break;
        case StorageOperation::Remount: mountCard(); ok = state_.card_state == CardState::Mounted; break;
        case StorageOperation::Unmount:
            closeCard(); state_.card_state = CardState::Unmounted;
            state_.card_bytes = state_.total_bytes = state_.free_bytes = 0; state_.fat_type = 0;
            state_.sd_error = 0;
            ok = true; break;
        case StorageOperation::Format: ok = formatCard(); break;
        case StorageOperation::EnterUsbMode:
            ok = saveBootMode(BootMode::UsbStorage);
            if (ok) { closeCard(); state_.card_state = CardState::Unmounted; }
            else state_.nvs_error = state_.error = -21;
            break;
        case StorageOperation::ExitUsbMode:
            ok = usb_.exitToLogger(); state_.usb = usb_.snapshot();
            if (!ok) state_.error = -31;
            break;
        default: break;
    }
    if(!usb_mode_ && !read_job && (job_.operation==StorageOperation::Remount || job_.operation==StorageOperation::Unmount || job_.operation==StorageOperation::Format) &&
       state_.card_state!=CardState::Mounted) releaseDataset(PlaceState::NoCard);
    state_.busy = has_read_;
    state_.operation = job_.operation;
    state_.operation_ok = ok;
    ++state_.revision;
    completion_ = {};
    completion_.kind = !ok ? (read_job ? MessageKind::ReadFailed : MessageKind::StorageError) :
                       read_job ? MessageKind::ReadCompleted :
                       job_.operation == StorageOperation::SaveSettings ? MessageKind::SettingsWritten :
                       MessageKind::StorageOperationCompleted;
    completion_.source = TaskId::Storage;
    completion_.sequence = ++sequence_;
    completion_.sent_at_ms = taskTimeMs();
    completion_.request_id = job_request_id_;
    auto& result = completion_.payload.storage_completion;
    result = {};
    result.request_id = job_request_id_;
    result.operation_id = job_.operation_id;
    state_.log = log_writer_.snapshot();
    result.snapshot = state_;
    result.read_kind = dataset_job ? StorageReadKind::Dataset : search_job ? StorageReadKind::Place :
                       load_job ? StorageReadKind::Settings : StorageReadKind::None;
    result.place = place;
    has_completion_ = true;
    job_kind_ = JobKind::None;
    if constexpr (config::DetailedDiagnostics) {
        if (search_job || dataset_job) {
            const auto cache = place_lookup_.cacheStats();
            const auto wordbook = place_lookup_.wordBookCacheStats();
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
                const auto& time = place_lookup_.readTiming();
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

void StorageTask::notify() {
    if (has_completion_ && sendSpeculative(notifications_, completion_) == SpeculativeResult::Queued) has_completion_ = false;
}

void StorageTask::sdBegin() {
    if (sd_depth_++) return;
    ++state_.sd.access_id;
    if (!++state_.sd.revision) ++state_.sd.revision;
    state_.sd.active = true; sd_dirty_ = true;
    notifyActivity();
}
void StorageTask::sdEnd() {
    if (!sd_depth_ || --sd_depth_) return;
    if (!++state_.sd.revision) ++state_.sd.revision;
    state_.sd.active = false; sd_dirty_ = true;
    notifyActivity();
}
void StorageTask::cacheState(RgCacheState cache, bool new_dataset) {
    if (!new_dataset && cache == state_.rg.state) return;
    if (new_dataset) ++state_.rg.dataset_generation;
    state_.rg.state = cache;
    if (!++state_.rg.revision) ++state_.rg.revision;
    rg_dirty_ = true;
    notifyActivity();
}
void StorageTask::indexProgress(void* pointer,const IndexProgress& progress) {
    auto& self=*static_cast<StorageTask*>(pointer);self.index_progress_=progress;
    self.index_progress_.token=self.dataset_request_.token;
    self.index_progress_.elapsed_ms=taskTimeMs()-self.index_started_at_;
    self.index_progress_dirty_=true;self.notifyActivity();
}
void StorageTask::notifyActivity() {
    if(index_progress_dirty_ && taskTimeMs()-index_progress_at_>=200) {
        AppNotification progress{};progress.kind=MessageKind::DatasetProgress;progress.source=TaskId::Storage;
        progress.payload.index_progress=index_progress_;
        if(sendSpeculative(notifications_,progress)==SpeculativeResult::Queued) {index_progress_dirty_=false;index_progress_at_=taskTimeMs();}
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
void StorageTask::flushLog(bool force) {
    const auto log = log_writer_.snapshot();
    const auto now = taskTimeMs();  // Sample time after the producer-published timestamp.
    const bool due = log.buffered && now - log.buffer_since_ms >= FlushIntervals[state_.settings.flush_interval] * 1000u;
    if ((!force && !log_writer_.needsFlush() && !due) || static_cast<int32_t>(now - next_log_retry_) < 0) return;
    const bool ok = log_writer_.flush(now);
    state_.log = log_writer_.snapshot();
    state_.log_result = ok ? LogError::None : state_.log.error;
    next_log_retry_ = ok ? now : now + 5000;
    ++state_.revision;
    completion_ = {};
    completion_.kind = ok ? MessageKind::LogWritten : MessageKind::StorageError;
    completion_.source = TaskId::Storage;
    completion_.sent_at_ms = now;
    completion_.payload.storage_completion.snapshot = state_;
    completion_.payload.storage_completion.read_kind = StorageReadKind::Log;
    has_completion_ = true;
    Console.printf("[LOG] saved=%lu buffered=%u closing=%u error=%u\n",
                  static_cast<unsigned long>(state_.log.saved), state_.log.buffered,
                  state_.log.closing, static_cast<unsigned>(state_.log.error));
}

void StorageTask::stepLogRead() {
    if(!read_prepared_ && !screen_resources_.logs.done()) {
        const auto log=log_writer_.snapshot();
        if(state_.card_state!=CardState::Mounted) screen_resources_.logs.fail(LogReadError::NoCard);
        else if(screen_resources_.logs.result().request.kind!=LogReadKind::Delete && log.session_id==read_barrier_session_ && log.saved<read_barrier_) {
            if(log.error!=LogError::None && static_cast<int32_t>(taskTimeMs()-next_log_retry_)<0)
                screen_resources_.logs.fail(LogReadError::SaveFailed);
            else { flushLog(true); return; }
        } else read_prepared_=true;
    }
    if(!screen_resources_.logs.done()) screen_resources_.logs.step(&log_writer_);
    if(!screen_resources_.logs.done()) return;
    has_read_=false; state_.busy=false; ++state_.revision;
    state_.log=log_writer_.snapshot();
    completion_={}; completion_.source=TaskId::Storage; completion_.request_id=read_request_id_;
    completion_.sequence=++sequence_; completion_.sent_at_ms=taskTimeMs();
    const auto& read=screen_resources_.logs.result();
    completion_.kind=read.error==LogReadError::None?MessageKind::ReadCompleted:MessageKind::ReadFailed;
    auto& result=completion_.payload.storage_completion;
    result.request_id=read_request_id_; result.snapshot=state_; result.read_kind=StorageReadKind::LogBrowse;
    result.logs=read; has_completion_=true;
    if constexpr (config::DetailedDiagnostics) Console.printf("[BROWSE] kind=%u token=%lu error=%u count=%u records=%lu start=%lu stack_free=%u\n",
                  static_cast<unsigned>(read.request.kind),static_cast<unsigned long>(read.request.token),static_cast<unsigned>(read.error),
                  read.count,static_cast<unsigned long>(read.summary.records),static_cast<unsigned long>(read.request.start),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void StorageTask::stepMap() {
    if(screen_resources_.map.reading()) {
        const auto request=screen_resources_.map.info().request;
        const auto started=config::DetailedDiagnostics ? taskTimeMs() : 0;
        reportMapHeap("read-begin",request.token);
        screen_resources_.map.loaded(readBoundaryMap(DatasetCatalog{}.directory(state_.settings.dataset),
            state_.settings.wordbook_language,request,drive_,this));
        if constexpr (config::DetailedDiagnostics) Console.printf("[MAP] read token=%lu error=%u elapsed_ms=%lu heap=%u\n",
            static_cast<unsigned long>(request.token),static_cast<unsigned>(screen_resources_.map.info().error),
            static_cast<unsigned long>(taskTimeMs()-started),ESP.getFreeHeap());
        reportMapHeap("render-begin",request.token);
    } else {
        const bool was_complete=screen_resources_.map.complete();
        screen_resources_.map.step();
        if(!was_complete && screen_resources_.map.complete())reportMapHeap("render-done",screen_resources_.map.info().request.token);
    }
    if(screen_resources_.map.notificationPending()) {
        AppNotification event{};event.kind=MessageKind::MapCompleted;event.source=TaskId::Storage;
        event.request_id=screen_resources_.map.info().request.token;event.payload.map=screen_resources_.map.info();
        if(sendSpeculative(notifications_,event)==SpeculativeResult::Queued)screen_resources_.map.notificationSent();
    }
}

void StorageTask::run() {
    for (;;) {
        sendReturn();
        notify();
        StorageRequest request{};
        // Keep the one accepted operation/result until delivered. An incoming
        // read notification must never be consumed and silently discarded.
        if (!has_return_ && !hasJob() && !has_completion_ && inbox().receive(request)) { handle(request); sendReturn(); }
        // Display notifications never gate physical work. Only accepted job/results are retained.
        if (!has_return_ && hasJob()) completeJob();
        if (!usb_mode_ && !has_return_ && !hasJob() && !has_completion_) flushLog();
        if (!has_return_ && !hasJob() && !has_completion_ && has_read_ && (!usb_mode_ || usb_.local())) {
            if(usb_mode_ && usb_work_==UsbWork::PublishMenu) screen_resources_.logs.cancel();
            stepLogRead();
        }
        if(!usb_mode_ && !has_return_ && !hasJob() && !has_completion_ && !has_read_) stepMap();
        if (usb_mode_) {
            if(!has_return_ && !hasJob() && !has_read_ && !has_completion_) stepUsbExport();
            const auto view = usb_.snapshot();
            if (!(view == state_.usb)) { state_.usb = view; ++state_.revision; usb_dirty_ = true; }
            if (usb_dirty_) {
                AppNotification event{}; event.source = TaskId::Storage; event.kind = MessageKind::UsbStorageChanged;
                event.payload.usb_storage = state_;
                if (sendSpeculative(notifications_,event) == SpeculativeResult::Queued) usb_dirty_ = false;
            }
        }
        notifyActivity();
        notify();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

}  // namespace geoputer

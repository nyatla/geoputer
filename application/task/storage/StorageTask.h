#pragma once

#include <SPI.h>
#include <memory>
#include "SettingsStore.h"
#include "galuchat/PlaceLookup.h"
#include "log/LogWriter.h"
#include "log/LogReader.h"
#include "dataset/DatasetCatalog.h"
#include "usb/UsbStorage.h"
#include "export/LogExporter.h"
#include "../../TaskMessages.h"
#include "StorageScreenResources.h"
#include "StorageReplies.h"
#include "../../utils/Task.h"

namespace geoputer {

class StorageTask final : public Task<StorageRequest, 1>, private StorageObserver {
public:
    StorageTask(Queue<AppNotification, 8>& notifications, Queue<ReturnMessage, 1>& results)
        : notifications_(notifications), results_(results), log_writer_("/sd/logs", this), screen_resources_(this) { place_lookup_.setObserver(this); }
    // Runs in the App caller; touches only the shared RAM ring, never SD or task queues.
    LogError tryAppendLog(const LogCommand& command, LogSnapshot& snapshot);
    void setUsbMode(bool enabled) { usb_mode_ = enabled; } // Before starting the task.
protected:
    void run() override;
private:
    void stepMap();
    void completeDatasetJob();
    void clearDatasetSelection(DatasetView& view);
    void maintainDatasetIndex(DatasetView& view);
    void applyDatasetSelection(DatasetView& view);

    enum class JobKind : uint8_t { None, LoadSettings, SearchPlace, CheckDataset, BrowseDatasets, Operation };
    bool hasJob() const { return job_kind_ != JobKind::None; }
    void handle(const StorageRequest& request);
    void sendReturn();
    void completeJob();
    void notifyActivity();
    static void indexProgress(void*,const IndexProgress&);
    void sdBegin() override;
    void sdEnd() override;
    void cacheState(RgCacheState state, bool new_dataset = false) override;
    void flushLog(bool force=false);
    void stepLogRead();
    void handleUsbExport(const StorageRequest& request, SyncReturn& result);
    void stepUsbExport();
    void notify();
    void mountCard();
    void closeCard();
    bool formatCard();
    void configureDataset();
    void releaseDataset(PlaceState reason);
    bool loadSettings();
    bool saveSettings(const Settings& value);

    Queue<AppNotification, 8>& notifications_;
    Queue<ReturnMessage, 1>& results_;
    SettingsStore settings_store_;
    UsbStorage usb_;
    // Allocated only while USB export needs its two FILs and conversion buffers.
    std::unique_ptr<LogExporter> exporter_;
    enum class UsbWork : uint8_t { None, Prepare, Convert, PublishMenu, PublishResult };
    UsbWork usb_work_ = UsbWork::None;
    bool export_started_ = false;
    uint32_t export_progress_at_ = 0;
    PlaceLookup place_lookup_;
    LogWriter log_writer_;
    StorageScreenResources screen_resources_;
    uint64_t read_barrier_session_=0;
    uint32_t read_barrier_=0, read_request_id_=0, statistics_progress_at_=0;
    uint32_t browse_release_generation_=0;
    bool has_read_=false, read_prepared_=false;
    unsigned sd_depth_ = 0;
    bool sd_dirty_ = false, rg_dirty_ = false;
    uint32_t next_log_retry_ = 0;
    PlaceRequest place_request_{};
    SPIClass spi_{FSPI};  // LCD uses SPI3/HSPI. SD owns the other SPI peripheral.
    StorageSnapshot state_{};
    StorageReplies replies_;
    StorageCommand job_{};
    uint32_t job_request_id_ = 0;
    uint32_t sequence_ = 0;
    uint8_t drive_ = 0xFF;
    bool nvs_ready_ = false;
    bool mounted_ = false;
    bool initialized_ = false;
    bool usb_mode_ = false, usb_dirty_ = false;
    JobKind job_kind_ = JobKind::None;
    DatasetRequest dataset_request_{};
    IndexProgress index_progress_{};
    uint32_t index_started_at_=0,index_progress_at_=0;
    bool index_progress_dirty_=false;
};

}  // namespace geoputer

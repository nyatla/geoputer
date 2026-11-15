#pragma once

#include <SPI.h>
#include <memory>
#include "SettingsStore.h"
#include "galuchat/PlaceLookup.h"
#include "log/LogWriter.h"
#include "log/LogReader.h"
#include "dataset/DatasetCatalog.h"
#include "../../GpsMessages.h"
#include "StorageScreenResources.h"
#include "GpsStorageResources.h"
#include "StorageIndexLease.h"
#include "StorageReplies.h"
#include "../../utils/Task.h"

namespace geoputer {

class GpsStorageTask final : public Task<StorageRequest, 1>, private StorageObserver {
public:
    GpsStorageTask(Queue<AppNotification,8>& notifications,Queue<ReturnMessage,1>& results)
        :notifications_(notifications),results_(results),resources_(this){}
    LogError tryAppendLog(const LogCommand&,LogSnapshot&);
protected:
    void run() override;
private:
    LogSnapshot logSnapshot()const{return resources_.log_writer_.snapshot();}
    void initializeCardHardware();
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
    void notify();
    void mountCard();
    void closeCard();
    bool formatCard();
    bool prepareDirectories();
    void configureDataset();
    void loadAlarmProfile();
    bool saveAlarmProfile();
    void releaseDataset(PlaceState reason);
    bool loadSettings();
    bool saveSettings(const Settings& value,const DatasetId* id=nullptr);

    Queue<AppNotification, 8>& notifications_;
    Queue<ReturnMessage, 1>& results_;
    SettingsStore settings_store_;
    GpsStorageResources& gpsState(){return resources_;}
    const GpsStorageResources& gpsState()const{return resources_;}
    StorageWorkArea& work(){return resources_.screen_resources_.work;}
    const StorageWorkArea& work()const{return resources_.screen_resources_.work;}
    uint64_t read_barrier_session_=0;
    uint32_t read_barrier_=0, read_request_id_=0, statistics_progress_at_=0;
    uint32_t browse_release_generation_=0;
    bool has_read_=false, read_prepared_=false;
    unsigned sd_depth_ = 0;
    bool sd_dirty_ = false, rg_dirty_ = false;
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
    JobKind job_kind_ = JobKind::None;
    // Destroy readers/writers while observer state and SPI are still alive.
    GpsStorageResources resources_;

};

}  // namespace geoputer

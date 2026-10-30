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
#include "../../ApplicationModeMemory.h"
#include "StorageIndexLease.h"
#include "StorageReplies.h"
#include "../../utils/Task.h"

namespace geoputer {

class StorageTask final : public Task<StorageRequest, 1>, private StorageObserver {
public:
    StorageTask(Queue<AppNotification, 8>& notifications, Queue<ReturnMessage, 1>& results,ApplicationModeMemory& memory)
        : notifications_(notifications), results_(results),mode_resources_(memory) {}
    // Application lifetime; workers/loans must be quiescent before destruction.
    // Release while this StorageObserver and the common task context still live.
    ~StorageTask() override {mode_resources_.destroyForShutdown();}
    // Runs in the App caller; touches only the shared RAM ring, never SD or task queues.
    LogError tryAppendLog(const LogCommand& command, LogSnapshot& snapshot);
    bool setWifiMode() {
        if(initialized_ || mode_resources_.selected())return false;
        const bool ok=mode_resources_.selectWifi();if(ok)wifi_mode_=true;return ok;
    }
    bool requestWifiExit(){return requestWifiControl(MessageKind::StopWifi);}
    bool requestWifiRegenerate(){return requestWifiControl(MessageKind::RegenerateWifi);}
    bool setUsbMode(bool enabled) { // Before starting the task; never reconstruct an active mode.
        if(initialized_)return false;
        const bool ok=enabled?mode_resources_.selectUsb(static_cast<StorageObserver*>(this)):mode_resources_.selectGps(notifications_,results_,static_cast<StorageObserver*>(this));
        if(ok)usb_mode_=enabled;
        return ok;
    }
protected:
    void run() override;
private:
    LogSnapshot logSnapshot()const{return (usb_mode_ || wifi_mode_)?LogSnapshot{}:gpsMode().log_writer_.snapshot();}
    bool requestWifiControl(MessageKind kind) {
        if(!wifi_mode_)return false;
        StorageRequest command{};command.kind=kind;command.source=TaskId::Application;
        return sendSpeculative(inbox(),command)==SpeculativeResult::Queued;
    }
    void runWifi();
    void pollWifiControl();
    void initializeCardHardware();
    static void wifiChanged(void*);
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
    void loadAlarmProfile();
    bool saveAlarmProfile();
    void releaseDataset(PlaceState reason);
    bool loadSettings();
    bool saveSettings(const Settings& value);

    Queue<AppNotification, 8>& notifications_;
    Queue<ReturnMessage, 1>& results_;
    SettingsStore settings_store_;
    ApplicationModeMemory& mode_resources_; // App reserves it; this task alone operates its Storage subobject.
    using UsbWork=StorageUsbResources::Work;
    StorageGpsResources& gpsMode(){return mode_resources_.gps().storage;}
    const StorageGpsResources& gpsMode()const{return mode_resources_.gps().storage;}
    StorageUsbResources& usbMode(){return mode_resources_.usb().storage;}
    const StorageUsbResources& usbMode()const{return mode_resources_.usb().storage;}
    StorageWorkArea& work(){return usb_mode_?usbMode().work_:gpsMode().screen_resources_.work;}
    const StorageWorkArea& work()const{return usb_mode_?usbMode().work_:gpsMode().screen_resources_.work;}
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
    bool usb_mode_ = false, usb_dirty_ = false, wifi_mode_ = false, wifi_dirty_ = false;
    JobKind job_kind_ = JobKind::None;

};

}  // namespace geoputer

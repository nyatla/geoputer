#pragma once

#include "TaskMessages.h"
#include "utils/SyncClient.h"
#include "task/ui/UiTask.h"
#include "task/gps/GpsTask.h"
#include "task/storage/StorageTask.h"
#include "task/storage/PlaceSearchFlow.h"

namespace geoputer {

// Runs on Arduino's main loopTask; no additional Application FreeRTOS task is created.
class Application {
public:
    Application() : sync_(results_), ui_(notifications_, results_), gps_(notifications_, results_), storage_(notifications_, results_) {}
    bool begin(bool usb_mode = false);
    void runOnce();

private:
    bool callUi(UiRequest& request, SyncReturn& result);
    template <typename Request>
    bool callTask(TaskId peer, Queue<Request, 1>& destination, Request& request, SyncReturn& result) {
        return awaitSyncResult(sync_.sendSync(peer, destination, request, pdMS_TO_TICKS(50)), result);
    }
    bool awaitSyncResult(SyncSendResult sent, SyncReturn& result);
    bool publishUiContent(uint32_t action_id = 0, ActionResult action_result = ActionResult::None);
    bool setScreen(ScreenId target, uint32_t action_id = 0,
                   ActionResult action_result = ActionResult::None);
    void handleNotification(const AppNotification& event);
    void drainNotifications();
    void readUiContent();
    // Keep the transfer frame out of action dispatch even under optimization.
    [[gnu::noinline]] bool readUiSnapshot(UiSnapshot& content);
    void applyStorage(const StorageSnapshot& state);
    bool operateStorage(const UiAction& action);
    bool beginUsb();
    bool operateBootMode(const UiAction& action);
    bool operateUsbExport(const UiAction& action);
    bool usbSelecting() const { return usb_mode_ && screen_==ScreenId::UsbStorage && data_.storage.export_job.phase==ExportPhase::Selecting; }
    void message(const char* value);
    void refreshPlace();
    bool canSearchPlace() const;
    void requestPlace();
    void advanceBoot();
    void bootChanged();
    void adoptLogSample();
    bool appendPendingLog();
    void processLog();
    void updateLogElapsed();
    bool logRequest(MessageKind kind, const LogCommand& command);
    bool queueLogAction(const UiAction& action);
    void prepareLogScreen(ScreenId target,ScreenId previous);
    bool operateBrowser(const UiAction& action);
    void processBrowser();
    void completeBrowser(const LogReadReply& reply);
    bool showCachedPoint();
    bool operateDataset(const UiAction& action);
    bool requestDataset(DatasetRequest request);
    void completeDataset(const DatasetView& view);
    bool datasetChangeReady() const;

    Queue<AppNotification, 8> notifications_;
    Queue<ReturnMessage, 1> results_;
    SyncClient sync_;
    UiTask ui_;
    GpsTask gps_;
    StorageTask storage_;
    ScreenId screen_ = ScreenId::LivePlace;
    uint32_t generation_ = 0;
    uint32_t last_processed_action_ = 0;
    UiData data_{};
    bool ui_changed_ = false;
    bool usb_mode_ = false;
    bool ui_data_dirty_ = false;
    DisplayMode mode_ = DisplayMode::View;
    uint32_t storage_operation_id_ = 0;
    LogCommand log_pending_record_{};
    UiAction log_control_{};
    bool log_pending_ = false, log_accepting_ = false;
    uint32_t log_seen_seq_ = 0, log_adopt_at_ = 0, log_retry_at_ = 0, log_elapsed_at_ = 0;
    bool log_adopted_ = false;
    LogReadRequest browse_request_{};
    uint8_t browse_records_[LogReadCount][32]{};
    uint32_t browse_token_=0, browse_waiting_=0, browse_retry_at_=0, browse_cache_start_=0;
    uint8_t browse_cache_count_=0;
    bool browse_need_=false, browse_cancel_=false, browse_single_=false;
    PlaceSearchFlow place_flow_;
    uint32_t dataset_token_ = 0, dataset_waiting_ = 0;
    bool dataset_applying_ = false, restart_pending_ = false;
    uint32_t place_request_id_ = 0x90000000u;
    enum class BootStage { NeedSettings, WaitSettings, NeedDataset, WaitDataset, Complete };
    BootStage boot_stage_ = BootStage::NeedSettings;
    uint32_t boot_completed_at_ = 0;
    static constexpr uint32_t BootSettingsRequest = 0x80000001u;
    static constexpr uint32_t BootDatasetRequest = 0x80000002u;
};

}  // namespace geoputer

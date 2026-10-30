#pragma once

#include "TaskMessages.h"
#include "alarm/AlarmEngine.h"
#include "AppScreenResources.h"
#include "map/MapTracking.h"
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
    void processAlarm();
    void requestAlarmSample();
    void completeAlarmSample();
    bool alarmAvailable()const;
    bool playAlarm(const alarm::Settings&,bool preview=false);
    void stopAlarm();
    [[gnu::noinline]] bool audioCommand(const alarm::AudioCommand&);
    void processMap();
    bool changeMapScale(uint8_t scale);
    void completeMap(const map::FrameInfo& info);
    void mapState(bool busy,map::Error error);
    map::Error mapAvailability() const;
    [[gnu::noinline]] bool mapStorageCommand(MessageKind kind,const map::Request& command);
    [[gnu::noinline]] bool mapUiCommand(MessageKind kind,const map::FrameInfo& info,map::BufferLease* buffer=nullptr);
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
    [[gnu::noinline]] void processPlace();
    [[gnu::noinline]] bool placeUiCommand(MessageKind kind,const PlaceRequest& request,map::BufferLease* buffer=nullptr);
    void advanceBoot();
    void processDatasetCheck();
    void initializeGps();
    bool ensureGpsReception();
    void enterMaintenance();
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
    void processReplay();
    void completeBrowser(const LogReadReply& reply);
    bool showCachedPoint();
    bool operateDataset(const UiAction& action);
    bool requestDataset(DatasetRequest request);
    void completeDataset(const DatasetCompletion& completion);
    void processDataset();
    [[gnu::noinline]] bool datasetUiCommand(MessageKind,const DatasetCompletion&,map::BufferLease* =nullptr,const IndexProgress* =nullptr);
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
    AppScreenResources screen_resources_;
    // Monotonic tokens and cancellation survive display-resource replacement.
    uint32_t browse_token_=0,browse_waiting_=0,browse_release_generation_=0;
    bool browse_cancel_=false;
    map::Request map_request_{},map_session_{};
    bool map_session_close_=false,map_session_ready_=false;
    uint32_t map_token_=0xa0000000u;
    bool map_cancel_=false,map_storage_owned_=false;
    alarm::Engine alarm_engine_;
    alarm::SoundSession alarm_sound_;
    uint32_t alarm_profile_revision_=0,alarm_identity_=0,alarm_sample_seq_=0;
    uint8_t alarm_dataset_=0;
    bool alarm_audio_owned_=false,alarm_preview_=false;
    PlaceSearchFlow place_flow_;
    // Loan ownership survives screen replacement and accepted async work.
    PlaceRequest place_borrow_{};
    bool place_completion_ready_=false,place_queued_=false,place_discard_=false;
    uint32_t dataset_token_ = 0, dataset_waiting_ = 0;
    DatasetRequest dataset_borrow_{},dataset_next_{};
    IndexProgress dataset_progress_{};
    DatasetError dataset_error_=DatasetError::None;
    bool dataset_pending_=false,dataset_ready_=false,dataset_queued_=false,dataset_discard_=false,dataset_progress_dirty_=false;
    bool dataset_applying_ = false, restart_pending_ = false;
    bool dataset_check_needed_ = false;
    uint32_t dataset_check_waiting_ = 0;
    uint32_t place_request_id_ = 0x90000000u;
    enum class BootStage { NeedSettings, WaitSettings, SelectMode, NeedDataset, WaitDataset, Complete };
    BootStage boot_stage_ = BootStage::NeedSettings;
    uint32_t boot_completed_at_ = 0;
    uint32_t boot_selection_at_ = 0;
    static constexpr uint32_t BootSettingsRequest = 0x80000001u;
    static constexpr uint32_t BootDatasetRequest = 0x80000002u;
};

}  // namespace geoputer

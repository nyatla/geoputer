#pragma once

#include "GpsMessages.h"
#include "alarm/AlarmEngine.h"
#include "GpsAppState.h"
#include "map/MapTracking.h"
#include "utils/SyncClient.h"
#include "task/ui/GpsUiTask.h"
#include "task/gps/GpsTask.h"
#include "task/storage/GpsStorageTask.h"
#include "task/storage/PlaceSearchFlow.h"

namespace geoputer {

// Runs on Arduino's main loopTask; no additional GpsApplication FreeRTOS task is created.
class GpsApplication {
public:
    GpsApplication() : sync_(results_), ui_(notifications_, results_), storage_(notifications_, results_),gps_(notifications_,results_) {}
    bool begin();
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
    bool operateBootMode(const UiAction& action);
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
    GpsUiTask ui_;
    GpsStorageTask storage_;
    ScreenId screen_ = ScreenId::LivePlace;
    uint32_t generation_ = 0;
    uint32_t last_processed_action_ = 0;
    UiData data_{};
    bool ui_changed_ = false;
    bool ui_data_dirty_ = false;
    DisplayMode mode_ = DisplayMode::View;
    uint32_t storage_operation_id_ = 0;
    uint32_t browse_token_=0,browse_waiting_=0,browse_release_generation_=0;
    bool browse_cancel_=false,restart_pending_=false;
    using BootStage=AppBootStage;
    GpsAppState& gpsState(){return state_;}
    const GpsAppState& gpsState()const{return state_;}
    AppScreenResources& screenResources(){return state_.screen_resources_;}
    const AppScreenResources& screenResources()const{return state_.screen_resources_;}
    GpsTask gps_;
    GpsAppState state_;
    static constexpr uint32_t BootSettingsRequest = 0x80000001u;
    static constexpr uint32_t BootDatasetRequest = 0x80000002u;
};

}  // namespace geoputer

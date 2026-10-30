#if defined(ARDUINO_ARCH_ESP32)
#include "StorageTask.h"
#include "../../utils/Console.h"
#include "../../utils/SyncClient.h"
namespace geoputer {
void StorageTask::pollWifiControl() {
    StorageRequest request{};
    if(inbox().receive(request) && request.source==TaskId::Application) {
        auto& service=mode_resources_.wifi().storage.service;
        if(request.kind==MessageKind::StopWifi)service.requestExit();
        else if(request.kind==MessageKind::RegenerateWifi)service.requestRegenerate();
    }
}
void StorageTask::wifiChanged(void* context) {
    auto& task=*static_cast<StorageTask*>(context);
    task.pollWifiControl();
    task.state_.wifi=task.mode_resources_.wifi().storage.service.view();
    ++task.state_.revision;task.wifi_dirty_=true;
    AppNotification event{};event.source=TaskId::Storage;event.kind=MessageKind::WifiStorageChanged;
    event.payload.usb_storage=task.state_;
    if(sendSpeculative(task.notifications_,event)==SpeculativeResult::Queued)task.wifi_dirty_=false;
}
void StorageTask::runWifi() {
    auto& service=mode_resources_.wifi().storage.service;
    for(;;) {
        sendReturn();
        if(!initialized_ && !replies_.returnPending()) {
            StorageRequest request{};
            if(inbox().receive(request) && request.source==TaskId::Application && request.kind==MessageKind::InitializeStorage) {
                initializeCardHardware();nvs_ready_=settings_store_.begin();
                state_.settings=defaultSettings();state_.settings_state=nvs_ready_?settings_store_.load(state_.settings):SettingsState::Unavailable;
                mountCard();state_.revision=1;initialized_=true;
                service.setStatus(state_);
                const bool ready=service.begin(drive_,state_.card_state==CardState::Mounted,this,wifiChanged,this);
                auto& reply=replies_.beginReturn();prepareSyncReturn(reply,TaskId::Storage,request,++sequence_);
                reply.payload.result.request_kind=request.kind;reply.payload.result.status=ready?OperationStatus::Ok:OperationStatus::Unavailable;
                state_.wifi=service.view();reply.payload.result.storage=state_;
                sendReturn();
            }
        } else if(initialized_) {
            pollWifiControl();service.step();notifyActivity();
            if(wifi_dirty_)wifiChanged(this);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
}
#endif

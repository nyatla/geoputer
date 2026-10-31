#include "WifiApplication.h"
#include <Arduino.h>
#include <tusb.h>
namespace geoputer {
bool WifiApplication::begin(){
 if(!events_.initialize()||!results_.initialize()||!ui_.initialize()||!ui_.start("WifiUI",8192,1))return false;
 Channels::UiRequest ui{};ui.kind=MessageKind::SetUiScreen;ui.data=data_;Channels::Result result{};
 if(!call(TaskId::UI,ui_.inbox(),ui,result)||result.status!=OperationStatus::Ok)return false;
 if(!storage_.initialize()||!storage_.start("WifiStorage",12288,1))return false;
 Channels::StorageRequest request{};request.kind=MessageKind::InitializeStorage;
 if(!call(TaskId::Storage,storage_.inbox(),request,result))return false;
 apply(result.storage);return publish();
}
bool WifiApplication::publish(uint32_t processed,ActionResult status){Channels::UiRequest request{};request.kind=MessageKind::UpdateUiContent;request.data=data_;const auto sent_revision=data_.storage.revision;const auto sent_sd=data_.storage.sd.revision;request.processed=processed;request.result=status;Channels::Result result{};const bool ok=call(TaskId::UI,ui_.inbox(),request,result)&&result.status==OperationStatus::Ok;if(ok)dirty_=data_.storage.revision!=sent_revision||data_.storage.sd.revision!=sent_sd;return ok;}
void WifiApplication::drain(){Channels::Event event{};while(events_.receive(event)){if(event.source==TaskId::UI)ui_changed_=true;else if(event.source==TaskId::Storage)apply(event.storage);}}
void WifiApplication::apply(const WifiState& state){if(!data_.storage.revision||newerRevision(state.revision,data_.storage.revision)){data_.storage=state;data_.settings=state.settings;dirty_=true;}else if(newerRevision(state.sd.revision,data_.storage.sd.revision)){data_.storage.sd=state.sd;dirty_=true;}}
void WifiApplication::runOnce(){drain();if(data_.storage.wifi.restart_ready){publish();tud_disconnect();vTaskDelay(pdMS_TO_TICKS(150));ESP.restart();return;}
 if(ui_changed_){ui_changed_=false;Channels::UiRequest get{};get.kind=MessageKind::GetUiContent;Channels::Result result{};if(call(TaskId::UI,ui_.inbox(),get,result)&&result.status==OperationStatus::Ok && result.action.id){Channels::StorageRequest command{};command.source=TaskId::Application;command.command.action=result.action.kind;const bool ok=sendSpeculative(storage_.inbox(),command)==SpeculativeResult::Queued;publish(result.action.id,ok?ActionResult::Applied:ActionResult::Rejected);}}
 if(dirty_)publish();else {Channels::Event event{};if(events_.receive(event,pdMS_TO_TICKS(50))){if(event.source==TaskId::UI)ui_changed_=true;else apply(event.storage);}}
}
}

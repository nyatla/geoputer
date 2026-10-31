#include "UsbApplication.h"
#include <Arduino.h>
#include <tusb.h>
namespace geoputer {
bool UsbApplication::begin(){
 if(!events_.initialize()||!results_.initialize()||!ui_.initialize()||!ui_.start("UsbUI",8192,1))return false;
 Channels::UiRequest ui{};ui.kind=MessageKind::SetUiScreen;ui.data=data_;Channels::Result result{};
 if(!call(TaskId::UI,ui_.inbox(),ui,result)||result.status!=OperationStatus::Ok)return false;
 if(!storage_.initialize()||!storage_.start("UsbStorage",12288,1))return false;
 Channels::StorageRequest request{};request.kind=MessageKind::InitializeStorage;
 if(!call(TaskId::Storage,storage_.inbox(),request,result))return false;
 apply(result.storage);return publish();
}
bool UsbApplication::publish(uint32_t processed,ActionResult status){Channels::UiRequest request{};request.kind=MessageKind::UpdateUiContent;request.data=data_;const auto sent_revision=data_.storage.revision;const auto sent_sd=data_.storage.sd.revision;request.processed=processed;request.result=status;Channels::Result result{};const bool ok=call(TaskId::UI,ui_.inbox(),request,result)&&result.status==OperationStatus::Ok;if(ok)dirty_=data_.storage.revision!=sent_revision||data_.storage.sd.revision!=sent_sd;return ok;}
void UsbApplication::drain(){Channels::Event event{};while(events_.receive(event)){if(event.source==TaskId::UI)ui_changed_=true;else if(event.source==TaskId::Storage)apply(event.storage);}}
void UsbApplication::apply(const UsbState& state){if(!data_.storage.revision||newerRevision(state.revision,data_.storage.revision)){const auto previous=data_.storage.export_job.phase;data_.storage=state;data_.settings=state.settings;if(previous!=ExportPhase::Selecting&&state.export_job.phase==ExportPhase::Selecting)need_list_=true;dirty_=true;}else if(newerRevision(state.sd.revision,data_.storage.sd.revision)){data_.storage.sd=state.sd;dirty_=true;}}
void UsbApplication::runOnce(){drain();if(data_.storage.restart_ready){publish();tud_disconnect();vTaskDelay(pdMS_TO_TICKS(150));ESP.restart();return;}
 if(need_list_&&!data_.storage.busy){need_list_=false;Channels::StorageRequest request{};request.kind=MessageKind::ListLogs;request.command.action=UsbActionKind::List;Channels::Result result{};if(call(TaskId::Storage,storage_.inbox(),request,result))apply(result.storage);}
 if(ui_changed_){ui_changed_=false;Channels::UiRequest get{};get.kind=MessageKind::GetUiContent;Channels::Result result{};
 if(call(TaskId::UI,ui_.inbox(),get,result)&&result.status==OperationStatus::Ok&&result.action.id){const auto action=result.action;Channels::StorageRequest request{};request.kind=MessageKind::UsbExportOperation;auto& command=request.command;command.action=action.kind;command.format=action.format;command.direction=action.direction;bool valid=true;
 if(action.kind==UsbActionKind::Start){valid=action.index<data_.storage.files.count&&data_.storage.export_job.phase==ExportPhase::Selecting;if(valid)std::memcpy(command.name,data_.storage.files.names[action.index],sizeof(command.name));}
 else if(action.kind==UsbActionKind::List){const auto& files=data_.storage.files;valid=files.count>0;if(valid)std::memcpy(command.name,files.names[action.direction==LogListDirection::Older?files.count-1:0],sizeof(command.name));}
 const bool ok=valid&&call(TaskId::Storage,storage_.inbox(),request,result)&&result.status==OperationStatus::Ok;if(ok)apply(result.storage);publish(action.id,ok?ActionResult::Applied:ActionResult::Rejected);
 }}
 if(dirty_)publish();else {Channels::Event event{};if(events_.receive(event,pdMS_TO_TICKS(50))){if(event.source==TaskId::UI)ui_changed_=true;else apply(event.storage);}}
}
}

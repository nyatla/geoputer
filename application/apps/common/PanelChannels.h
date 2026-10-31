#pragma once
#include "../../Settings.h"
#include "../../utils/MessageTypes.h"
#include "../../utils/SyncClientBase.h"
#include "../../task/ui/UiPrimitives.h"
namespace geoputer {
struct PanelSettings {UiLanguage language=UiLanguage::Japanese;uint8_t brightness=40;};
template<class View,class Action,class Command,class State> struct PanelChannels {
 struct UiRequest:MessageHeader {View data{};uint32_t processed=0;ActionResult result=ActionResult::None;};
 struct StorageRequest:MessageHeader {Command command{};};
 struct Result {MessageKind request_kind{};OperationStatus status=OperationStatus::InvalidRequest;Action action{};State storage{};};
 struct Returned:MessageHeader {struct {Result result{};}payload;};
 struct Event:MessageHeader {State storage{};};
 using Events=Queue<Event,8>;using Results=Queue<Returned,1>;
 using Sync=BasicSyncClient<Result,Returned>;
 static void reply(Returned& out,TaskId task,const MessageHeader& request,OperationStatus status) {
  out={};out.source=task;out.kind=MessageKind::SyncReturn;out.request_id=request.request_id;
  out.payload.result.request_kind=request.kind;out.payload.result.status=status;
 }
};
} // namespace geoputer

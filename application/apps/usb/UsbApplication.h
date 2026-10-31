#pragma once
#include "UsbUiTask.h"
#include "UsbStorageTask.h"
namespace geoputer {
class UsbApplication {
public:
 UsbApplication():sync_(results_),ui_(events_,results_),storage_(events_,results_){}
 bool begin();void runOnce();
private:
 using Channels=UsbChannels;
 template<class Request>bool call(TaskId peer,Queue<Request,1>& destination,Request& request,typename Channels::Result& result){
  if(sync_.sendSync(peer,destination,request,pdMS_TO_TICKS(50))!=SyncSendResult::AwaitingResult)return false;
  for(;;){const auto status=sync_.receiveSyncResult(result,pdMS_TO_TICKS(50));if(status==SyncReceiveResult::Returned)return true;drain();vTaskDelay(1);}
 }
 void drain();void apply(const UsbState& state);bool publish(uint32_t processed=0,ActionResult result=ActionResult::None);
 Channels::Events events_;Channels::Results results_;Channels::Sync sync_;
 UsbUiTask ui_;UsbStorageTask storage_;UsbUiData data_{};
 bool ui_changed_=false,dirty_=false;
 bool need_list_=false;
};
}

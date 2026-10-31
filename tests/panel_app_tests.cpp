#include "apps/usb/UsbApplication.h"
#include "apps/wifi/WifiApplication.h"
#include <cstring>
#include <stdexcept>
#include <cstdio>
using namespace geoputer;
namespace {
UsbApplication* usb=nullptr;WifiApplication* wifi_app=nullptr;
UsbCommand last_usb{};WifiCommand last_wifi{};
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class App,class Channels>void uiPeer(App& app,QueueHandle_t queue){
 if(queue!=app.ui_.inbox().handle_)return;
 typename Channels::UiRequest request{};app.ui_.inbox().receive(request);
 typename Channels::Returned reply{};Channels::reply(reply,TaskId::UI,request,OperationStatus::Ok);
 if(request.kind==MessageKind::GetUiContent)reply.payload.result.action=app.ui_.model_.action();
 else app.ui_.model_.update(request.data,request.processed,request.result);
 check(app.results_.send(reply),"one result slot available for the pending call");
}
void route(QueueHandle_t queue){
 if(usb){uiPeer<UsbApplication,UsbChannels>(*usb,queue);
  if(queue==usb->storage_.inbox().handle_){UsbChannels::StorageRequest request{};usb->storage_.inbox().receive(request);last_usb=request.command;
   UsbChannels::Returned reply{};UsbChannels::reply(reply,TaskId::Storage,request,OperationStatus::Ok);reply.payload.result.storage=usb->data_.storage;
   if(last_usb.action==UsbActionKind::List)reply.payload.result.storage.files.loading=true;
   if(last_usb.action==UsbActionKind::Start)reply.payload.result.storage.export_job.phase=ExportPhase::Running;
   ++reply.payload.result.storage.revision;check(usb->results_.send(reply),"USB command has one synchronous result");
  }
 }
 if(wifi_app){uiPeer<WifiApplication,WifiChannels>(*wifi_app,queue);
  if(queue==wifi_app->storage_.inbox().handle_){WifiChannels::StorageRequest request{};wifi_app->storage_.inbox().receive(request);check(!request.request_id,"Wi-Fi control is speculative");last_wifi=request.command;}
 }
}
template<class App>void initialize(App& app){check(app.events_.initialize()&&app.results_.initialize()&&app.ui_.initialize()&&app.storage_.initialize(),"mode-local queues");}
}
namespace geoputer {
void UsbUiTask::run(){}void UsbStorageTask::run(){}void UsbStorageTask::sdBegin(){}void UsbStorageTask::sdEnd(){}
void WifiUiTask::run(){}void WifiStorageTask::run(){}void WifiStorageTask::sdBegin(){}void WifiStorageTask::sdEnd(){}
}
int main(){hostQueueHook=route;
 {
  UsbApplication app;usb=&app;initialize(app);UsbState state{};state.revision=1;state.usb.ready=true;app.apply(state);check(app.publish(),"initial USB UI snapshot");
  app.ui_.model_.key(UiKey::Enter);app.ui_changed_=true;app.runOnce();check(last_usb.action==UsbActionKind::Begin&&!app.ui_.model_.pending(),"USB action accepted and acknowledged");
  state.revision=3;state.usb.local=true;state.export_job.phase=ExportPhase::Selecting;
  UsbChannels::Event complete{};complete.source=TaskId::Storage;complete.storage=state;app.events_.send(complete);app.runOnce();
  check(last_usb.action==UsbActionKind::List&&app.ui_.model_.data().browse().loading,"acceptance triggers asynchronous list and loading state");
  complete.storage.revision=5;complete.storage.files.loading=false;complete.storage.files.count=1;complete.storage.files.revision=1;std::strcpy(complete.storage.files.names[0],"sample.gps.dat");app.events_.send(complete);app.runOnce();
  check(app.ui_.model_.usbPage()==UsbPage::Files&&!app.ui_.model_.data().browse().loading,"async list completion reaches the USB UI");
  app.ui_.model_.key(UiKey::Enter);app.ui_.model_.key(UiKey::Down);app.ui_.model_.key(UiKey::Enter);app.ui_changed_=true;app.runOnce();
  check(last_usb.action==UsbActionKind::Start&&last_usb.format==ExportFormat::Kml&&!std::strcmp(last_usb.name,"sample.gps.dat"),"USB App pins the selected filename and format");
  check(app.ui_.model_.usbPage()==UsbPage::Progress&&!app.ui_.model_.pending(),"recording-free USB progress uses only USB state");usb=nullptr;
 }
 {
  WifiApplication app;wifi_app=&app;initialize(app);WifiState state{};state.revision=1;state.wifi.phase=WifiPhase::Ready;std::strcpy(state.wifi.url,"http://192.168.4.1/");app.apply(state);check(app.publish(),"initial Wi-Fi UI snapshot");
  app.ui_.model_.key(UiKey::Recalculate);app.ui_.model_.key(UiKey::Right);app.ui_.model_.key(UiKey::Enter);app.ui_changed_=true;app.runOnce();
  check(last_wifi.action==WifiActionKind::Regenerate&&!app.ui_.model_.pending(),"Wi-Fi regeneration remains speculative and acknowledged");
  WifiChannels::Event event{};event.source=TaskId::Storage;event.storage=state;event.storage.revision=2;event.storage.wifi.clients=1;app.events_.send(event);app.runOnce();
  check(app.ui_.model_.wifiPage()==WifiPage::WebQr,"first client state is routed to Wi-Fi QR UI");
  app.ui_.model_.key(UiKey::Escape);app.ui_.model_.key(UiKey::Right);app.ui_.model_.key(UiKey::Enter);app.ui_changed_=true;app.runOnce();check(last_wifi.action==WifiActionKind::Exit,"exit goes only to Wi-Fi Storage");wifi_app=nullptr;
 }
 hostQueueHook=nullptr;std::puts("PASS: independent USB/Wi-Fi App dispatch, mode-local queues, asynchronous lists, export selection, QR state and speculative exit controls");
}

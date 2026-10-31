#include "WifiUiTask.h"
#include "../common/PanelUiLoop.h"
namespace geoputer {
void WifiUiTask::handle(const Channels::UiRequest& request,bool ready) {
 if(request.source!=TaskId::Application||!request.request_id)return;
 Channels::reply(reply_,TaskId::UI,request,ready?OperationStatus::Ok:OperationStatus::Unavailable);
 if(ready){if(request.kind==MessageKind::GetUiContent)reply_.payload.result.action=model_.action();else if(request.kind==MessageKind::SetUiScreen||request.kind==MessageKind::UpdateUiContent)model_.update(request.data,request.processed,request.result);else reply_.payload.result.status=OperationStatus::InvalidRequest;}
 reply_pending_=true;
}
void WifiUiTask::notify() {if(model_.revision()==notified_)return;Channels::Event event{};event.source=TaskId::UI;event.kind=MessageKind::UiContentChanged;if(sendSpeculative(events_,event)==SpeculativeResult::Queued)notified_=model_.revision();}
void WifiUiTask::render(PanelRenderer& renderer){qr_.update(model_.data().storage.wifi,model_.wifiPage());renderer.draw(model_,qr_);}
void WifiUiTask::run(){panelUiLoop(*this,BootMode::WifiManagement);}
}

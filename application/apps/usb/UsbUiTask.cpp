#include "UsbUiTask.h"
#include "../common/PanelUiLoop.h"
namespace geoputer {
void UsbUiTask::handle(const Channels::UiRequest& request,bool ready) {
 if(request.source!=TaskId::Application||!request.request_id)return;
 Channels::reply(reply_,TaskId::UI,request,ready?OperationStatus::Ok:OperationStatus::Unavailable);
 if(ready){if(request.kind==MessageKind::GetUiContent)reply_.payload.result.action=model_.action();else if(request.kind==MessageKind::SetUiScreen||request.kind==MessageKind::UpdateUiContent)model_.update(request.data,request.processed,request.result);else reply_.payload.result.status=OperationStatus::InvalidRequest;}
 reply_pending_=true;
}
void UsbUiTask::notify() {if(model_.revision()==notified_)return;Channels::Event event{};event.source=TaskId::UI;event.kind=MessageKind::UiContentChanged;if(sendSpeculative(events_,event)==SpeculativeResult::Queued)notified_=model_.revision();}
void UsbUiTask::render(PanelRenderer& renderer){renderer.draw(model_);}
void UsbUiTask::run(){panelUiLoop(*this,BootMode::UsbStorage);}
}

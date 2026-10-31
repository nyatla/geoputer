#pragma once
#include "UsbUiModel.h"
#include "../../utils/Task.h"
#include "../../task/ui/UiFrameBuffer.h"
namespace geoputer {
class UsbUiTask final:public Task<UsbChannels::UiRequest,1> {
public:
 using Channels=UsbChannels;
 UsbUiTask(Channels::Events& events,Channels::Results& results):events_(events),results_(results){}
 void handle(const Channels::UiRequest& request,bool ready);
 void notify();
 void render(class PanelRenderer& renderer);
protected:void run() override;
private:
 template<class T>friend void panelUiLoop(T&,BootMode);
 Channels::Events& events_;Channels::Results& results_;
 UiFrameBuffer frame_buffer_;UsbUiModel model_;
 Channels::Returned reply_{};bool reply_pending_=false;uint32_t notified_=0;
};
}

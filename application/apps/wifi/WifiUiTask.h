#pragma once
#include "WifiUiModel.h"
#include "../../utils/Task.h"
#include "../../task/ui/UiFrameBuffer.h"
#include "../../wifi/WifiQrCode.h"
namespace geoputer {
class WifiUiTask final:public Task<WifiChannels::UiRequest,1> {
public:
 using Channels=WifiChannels;
 WifiUiTask(Channels::Events& events,Channels::Results& results):events_(events),results_(results){}
 void handle(const Channels::UiRequest& request,bool ready);
 void notify();
 void render(class PanelRenderer& renderer);
protected:void run() override;
private:
 template<class T>friend void panelUiLoop(T&,BootMode);
 Channels::Events& events_;Channels::Results& results_;
 UiFrameBuffer frame_buffer_;WifiUiModel model_;
 Channels::Returned reply_{};bool reply_pending_=false;uint32_t notified_=0;
 wifi::QrCode qr_;
};
}

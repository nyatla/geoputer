#pragma once
#include <cstring>
#include "../../task/ui/UiFrameBuffer.h"
#include "../../task/ui/PixelFont.h"
#include "../../wifi/WifiQrCode.h"
#include "../wifi/WifiUiModel.h"
#include "../usb/UsbUiModel.h"
namespace geoputer {
class PanelRenderer {
public:
 explicit PanelRenderer(M5GFX& display):display_(display),frame_(&display),font_(display){}
 bool begin(void* memory,size_t bytes,uint8_t depth=config::UiColorDepth){display_.setRotation(1);return display_.width()==240 && display_.height()==135 && frame_.begin(depth,memory,bytes)&&font_.begin();}
 void draw(const WifiUiModel& model,const wifi::QrCode& qr);
 void draw(const UsbUiModel& model);
private:
 void text(int x,int y,const char* s,uint16_t color=TFT_WHITE){font_.draw(frame_,x,y,s,TextRole::UI,color);}
 void valueText(int l,int right,int y,const char* s,uint16_t color=TFT_WHITE){if(int(std::strlen(s))*8>right-l)s="OVR";text(right-int(std::strlen(s))*8,y,s,color);}
 void arrow(int x,int y,UiKey direction,bool enabled);
 void header(BatteryStatus battery,bool active);
 void dialog(const ChoiceDialog& dialog,UiLanguage language);
 template<class Model>void present(const Model& model,bool wifi);
 void drawUsb(const UsbUiModel& model);
 void drawWifi(const WifiUiModel& model);
 M5GFX& display_;UiCanvas frame_;PixelFont font_;const wifi::QrCode* wifi_qr_=nullptr;
};
}

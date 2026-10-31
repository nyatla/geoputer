#pragma once
#include "WifiMessages.h"
#include "../common/PanelModel.h"
namespace geoputer {
class WifiUiModel:public PanelModel<WifiUiData,WifiAction> {
public:
 void update(const WifiUiData& next,uint32_t processed,ActionResult=ActionResult::Applied){if(page_==WifiPage::ConnectionQr&&!data_.storage.wifi.clients&&next.storage.wifi.clients)page_=WifiPage::WebQr;updateData(next,processed);if(next.storage.wifi.phase==WifiPhase::Stopping)dialog_.close();}
 WifiPage wifiPage()const{return page_;}
 bool accepting()const{return !pending() && data_.storage.wifi.phase!=WifiPhase::Stopping;}
 UiShortcutGuide shortcutGuide()const {const auto active=accepting();if(confirming())return dialogGuide(active);UiShortcutGuide row;row.key(UiKey::Escape,active);row.group();row.key(UiKey::Recalculate,active&&data_.storage.wifi.url[0]);row.key(UiKey::Left,active);row.key(UiKey::Right,active);return row;}
 void key(UiKey k){if(k==UiKey::Back)k=UiKey::Escape;if(!accepting())return;
  if(dialog_.active()){const auto kind=dialog_.kind();const auto v=dialog_.keyPressed(k);if(v!=DialogInput::Ignored)changed();if(v==DialogInput::Accepted){beginAction();action_.kind=kind==DialogKind::RegenerateWifi?WifiActionKind::Regenerate:WifiActionKind::Exit;}return;}
  if(k==UiKey::Escape){dialog_.open(DialogKind::ExitWifiMode);changed();}
  else if(k==UiKey::Recalculate && data_.storage.wifi.url[0]){dialog_.open(DialogKind::RegenerateWifi);changed();}
  else if(k==UiKey::Left||k==UiKey::Right){page_=static_cast<WifiPage>((unsigned(page_)+(k==UiKey::Right?1:2))%3);changed();}
 }
private:WifiPage page_=WifiPage::ConnectionQr;
};
}

#pragma once
#include "../../task/ui/ChoiceDialog.h"
#include "../../task/ui/UiShortcutGuide.h"
#include "../../task/storage/StorageActivity.h"
namespace geoputer {
template<class View,class Action> class PanelModel {
public:
 const View& data()const{return data_;}const ChoiceDialog& dialog()const{return dialog_;}
 bool confirming()const{return dialog_.active();}const Action& action()const{return action_;}
 uint32_t revision()const{return revision_;}bool takeRedraw(){const bool v=redraw_;redraw_=false;return v;}
 BatteryStatus battery()const{return battery_;}
 void setBattery(int percent,bool charging,bool known){BatteryStatus next{static_cast<int8_t>(percent),charging,known};if(next.percent!=battery_.percent||next.charging!=battery_.charging||next.charge_known!=battery_.charge_known){battery_=next;redraw_=true;}}
 bool sdAccessLit()const{return data_.storage.sd.active || (sd_seen_ && now_-sd_at_<150);}
 void tick(uint32_t now){const bool lit=sdAccessLit();now_=now;if(lit!=sdAccessLit())redraw_=true;}
 bool pending()const{return action_.id!=0;}
protected:
 UiShortcutGuide dialogGuide(bool active)const {
  UiShortcutGuide row;row.key(UiKey::Escape,active);row.group();row.key(UiKey::Enter,active&&dialog_.choiceEnabled(dialog_.choice()));
  bool left=false,right=false;for(unsigned i=0;i<dialog_.choices();++i)if(dialog_.choiceEnabled(i)){left|=i<dialog_.choice();right|=i>dialog_.choice();}
  row.key(UiKey::Left,active&&left);row.key(UiKey::Right,active&&right);return row;
 }
 void updateData(const View& next,uint32_t processed){if(next.storage.sd.access_id!=data_.storage.sd.access_id){sd_seen_=true;sd_at_=now_;}data_=next;if(action_.id && action_.id==processed)action_={};redraw_=true;}
 void changed(){++revision_;redraw_=true;}
 void beginAction(){action_={};if(!++id_)++id_;action_.id=id_;changed();}
 View data_{};Action action_{};ChoiceDialog dialog_;
private:
 BatteryStatus battery_{};uint32_t revision_=1,id_=0,now_=0,sd_at_=0;bool redraw_=true,sd_seen_=false;
};
}

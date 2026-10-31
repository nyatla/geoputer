#pragma once
#include "UsbMessages.h"
#include "../common/PanelModel.h"
namespace geoputer {
class UsbUiModel:public PanelModel<UsbUiData,UsbUiAction> {
public:
 void update(const UsbUiData& next,uint32_t processed,ActionResult result=ActionResult::Applied){
  const auto previous=data_.storage.export_job.phase;const bool rejected=processed==action_.id && result==ActionResult::Rejected;
  if(await_page_&&!next.browse().loading&&next.browse().revision!=data_.browse().revision){selected_=page_direction_==LogListDirection::Older?0:next.browse().count?next.browse().count-1:0;await_page_=false;}
  updateData(next,processed);if(rejected)page_=return_page_;
  const auto phase=next.storage.export_job.phase;
  if(phase!=previous || (!pending() && page_==UsbPage::Busy)) {
   switch(phase){case ExportPhase::Idle:page_=UsbPage::Menu;selected_=0;break;case ExportPhase::WaitEject:page_=UsbPage::WaitEject;break;case ExportPhase::Preparing:case ExportPhase::Publishing:page_=UsbPage::Busy;break;
   case ExportPhase::Selecting:page_=UsbPage::Files;if(phase!=previous)selected_=0;break;case ExportPhase::Running:page_=UsbPage::Progress;break;case ExportPhase::Complete:page_=UsbPage::Complete;break;case ExportPhase::Failed:page_=UsbPage::Error;break;}
  }
  if(page_==UsbPage::Files && !next.browse().loading && selected_>=next.browse().count)selected_=next.browse().count?next.browse().count-1:0;
 }
 UsbPage usbPage()const{return page_;}uint8_t usbFile()const{return file_;}
 struct Snapshot{uint8_t selected_setting;};Snapshot snapshot()const{return {selected_};}
 bool accepting()const{return !pending() && !data_.storage.restart_ready;}
 UiShortcutGuide shortcutGuide()const {
  const auto active=accepting();if(confirming())return dialogGuide(active);UiShortcutGuide row;
  if(page_==UsbPage::Busy||page_==UsbPage::Progress)return row;
  row.key(UiKey::Escape,active);row.group();const auto& view=data_.browse();
  const bool enter=page_==UsbPage::Menu?(selected_||data_.storage.usb.ready):page_==UsbPage::Files?(!view.loading&&selected_<view.count&&view.error==LogReadError::None):true;
  row.key(UiKey::Enter,active&&enter);
  if(page_==UsbPage::Menu){row.key(UiKey::Up,active&&selected_>0);row.key(UiKey::Down,active&&selected_==0);}
  else if(page_==UsbPage::Files){const bool ready=active&&!view.loading;row.key(UiKey::Up,ready&&(selected_>0||view.has_newer));row.key(UiKey::Down,ready&&(selected_+1u<view.count||view.has_older));}
  else if(page_==UsbPage::Format){row.key(UiKey::Up,active&&selected_>0);row.key(UiKey::Down,active&&selected_<2);}
  return row;
 }
 void key(UiKey k){if(k==UiKey::Back)k=UiKey::Escape;if(!accepting())return;
  if(dialog_.active()){const auto v=dialog_.keyPressed(k);if(v!=DialogInput::Ignored)changed();if(v==DialogInput::Accepted)emit(UsbActionKind::Exit);return;}
  const auto& view=data_.browse();
  if(page_==UsbPage::Menu){if(k==UiKey::Up){selected_=0;changed();}else if(k==UiKey::Down){selected_=1;changed();}else if(k==UiKey::Escape||(k==UiKey::Enter&&selected_)){dialog_.open(DialogKind::ExitUsbMode);changed();}else if(k==UiKey::Enter&&data_.storage.usb.ready)emit(UsbActionKind::Begin);}
  else if(page_==UsbPage::WaitEject){if(k==UiKey::Escape||k==UiKey::Enter)emit(UsbActionKind::Publish);}
  else if(page_==UsbPage::Files){if(k==UiKey::Escape){emit(UsbActionKind::Publish);return;}if(view.loading)return;
   if(k==UiKey::Up){if(selected_){--selected_;changed();}else if(view.has_newer)emitList(LogListDirection::Newer);}
   else if(k==UiKey::Down){if(selected_+1<view.count){++selected_;changed();}else if(view.has_older)emitList(LogListDirection::Older);}
   else if(k==UiKey::Enter && selected_<view.count && view.error==LogReadError::None){file_=selected_;selected_=0;page_=UsbPage::Format;changed();}
  }else if(page_==UsbPage::Format){if(k==UiKey::Escape){page_=UsbPage::Files;selected_=file_;changed();}else if(k==UiKey::Up&&selected_){--selected_;changed();}else if(k==UiKey::Down&&selected_<2){++selected_;changed();}else if(k==UiKey::Enter){emit(UsbActionKind::Start);action_.index=file_;action_.format=static_cast<ExportFormat>(selected_);}}
  else if(page_==UsbPage::Complete||page_==UsbPage::Error){if(k==UiKey::Enter||k==UiKey::Escape)emit(UsbActionKind::Publish);}
 }
private:
 void emit(UsbActionKind k){return_page_=page_;beginAction();action_.kind=k;if(k!=UsbActionKind::List)page_=UsbPage::Busy;}
 void emitList(LogListDirection d){emit(UsbActionKind::List);action_.direction=d;page_direction_=d;await_page_=true;}
 UsbPage page_=UsbPage::Menu,return_page_=UsbPage::Menu;uint8_t selected_=0,file_=0;bool await_page_=false;LogListDirection page_direction_=LogListDirection::Older;
};
}

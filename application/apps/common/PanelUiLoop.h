#pragma once
#include "PanelRenderer.h"
#include "../../power/AudioPower.h"
#include "../../hardware/AdvKeyboardReader.h"
#include "../../task/ui/KeyBindings.h"
#include <M5Cardputer.h>
namespace geoputer {
// Hardware polling is a shared component; each caller owns a distinct typed task.
template<class TaskType>void panelUiLoop(TaskType& task,BootMode) {
 auto config=M5.config();config.internal_spk=false;config.internal_mic=false;M5Cardputer.begin(config,false);
 {power::AudioPower unused_audio;unused_audio.begin({false,false,true},taskTimeMs());}
 if(M5.getBoard()==m5::board_t::board_M5CardputerADV)M5Cardputer.Keyboard.begin(std::make_unique<AdvKeyboardReader>(M5Cardputer.Keyboard.maxKeys()));else M5Cardputer.Keyboard.begin();
 PanelRenderer renderer(M5Cardputer.Display);const bool ready=renderer.begin(task.frame_buffer_.data,sizeof(task.frame_buffer_.data));
 uint32_t held=0,repeat=0,battery_at=0;bool sampled=false;
 for(;;){
  if(task.reply_pending_ && task.results_.send(task.reply_))task.reply_pending_=false;
  typename TaskType::Channels::UiRequest request{};if(!task.reply_pending_&&task.inbox().receive(request))task.handle(request,ready);
  M5Cardputer.update();M5Cardputer.Keyboard.updateKeyList();M5Cardputer.Keyboard.updateKeysState();const auto& state=M5Cardputer.Keyboard.keysState();
  uint32_t pressed=0;for(const auto& pos:M5Cardputer.Keyboard.keyList()){const auto value=M5Cardputer.Keyboard.getKeyValue(pos).value_first;const auto key=mapPhysicalKey(value,state.fn);if(key==UiKey::Escape)pressed|=1;else if(key==UiKey::Left)pressed|=2;else if(key==UiKey::Right)pressed|=4;else if(key==UiKey::Up)pressed|=8;else if(key==UiKey::Down)pressed|=16;else if(key==UiKey::Recalculate)pressed|=64;}
  if(state.del)pressed|=1;
  if(state.enter)pressed|=32;
  const auto now=taskTimeMs();constexpr UiKey keys[]={UiKey::Escape,UiKey::Left,UiKey::Right,UiKey::Up,UiKey::Down,UiKey::Enter,UiKey::Recalculate};
  for(unsigned i=0;i<7;++i)if((pressed&(1u<<i))&&!(held&(1u<<i)))task.model_.key(keys[i]);
  if((pressed&24)!=(held&24))repeat=now+400;else if((pressed&24)&&static_cast<int32_t>(now-repeat)>=0){task.model_.key(pressed&8?UiKey::Up:UiKey::Down);repeat=now+100;}
  held=pressed;task.model_.tick(now);
  if(!sampled||now-battery_at>=5000){const auto charging=M5Cardputer.Power.isCharging();task.model_.setBattery(M5Cardputer.Power.getBatteryLevel(),charging==m5::Power_Class::is_charging,charging!=m5::Power_Class::charge_unknown);battery_at=now;sampled=true;}
  task.notify();if(ready&&task.model_.takeRedraw())task.render(renderer);
  vTaskDelay(pdMS_TO_TICKS(10));
 }
}
}

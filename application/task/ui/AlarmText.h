#pragma once
#include "GpsUiTypes.h"
#include <cstdio>
namespace geoputer {
inline constexpr unsigned AlarmRows=5,AlarmSoundRow=2,AlarmReloadRow=4;
inline unsigned alarmChoices(unsigned row) {
    switch(row){case 0:return 2;case 1:return 3;case AlarmSoundRow:return 5;case 3:return sizeof(alarm::Durations);default:return 0;}
}
inline uint8_t alarmValue(const alarm::Settings& s,unsigned row) {
    switch(row){case 0:return s.enabled;case 1:return unsigned(s.judgement);case AlarmSoundRow:return unsigned(s.tone);case 3:return s.duration;default:return 0;}
}
inline void setAlarmValue(alarm::Settings& s,unsigned row,uint8_t choice) {
    switch(row){case 0:s.enabled=choice!=0;break;case 1:s.judgement=static_cast<alarm::Judgement>(choice);break;
        case AlarmSoundRow:s.tone=static_cast<alarm::Tone>(choice);break;case 3:s.duration=choice;break;default:break;}
}
inline const char* alarmLabel(unsigned row,bool ja) {
    static constexpr const char* en[]={"Enabled","Region check","Sound","Duration","Reload"};
    static constexpr const char* jp[]={"通知","区域判定","通知音","通知時間","再読込"};
    return row<AlarmRows?(ja?jp[row]:en[row]):"--";
}
inline void alarmValueText(unsigned row,unsigned choice,bool ja,char* out,size_t size) {
    const char* value=nullptr;
    switch(row) {
        case 0:value=choice?"ON":"OFF";break;
        case 1:{static constexpr const char* en[]={"Immediate","Standard","Confirmed"};static constexpr const char* jp[]={"即時","標準","確定"};if(choice<3)value=ja?jp[choice]:en[choice];break;}
        case AlarmSoundRow:{static constexpr const char* en[]={"Single","Double","Rising","Falling","Chime"};static constexpr const char* jp[]={"単音","二連音","上昇音","下降音","チャイム"};if(choice<5)value=ja?jp[choice]:en[choice];break;}
        case 3:if(choice<sizeof(alarm::Durations)) {const auto seconds=alarm::Durations[choice];if(seconds==0)value=ja?"1回":"Once";else if(seconds==255)value=ja?"停止まで":"Until stop";else {std::snprintf(out,size,"%us",seconds);return;}}break;
        default:value="";break;
    }
    std::snprintf(out,size,"%s",value?value:"--");
}
} // namespace geoputer

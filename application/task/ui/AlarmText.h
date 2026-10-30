#pragma once
#include "UiTypes.h"
#include <cstdio>
namespace geoputer {
inline constexpr unsigned AlarmRows=8,AlarmTestRow=6,AlarmReloadRow=7;
inline unsigned alarmChoices(unsigned row,unsigned depth) {
    switch(row){case 0:return 2;case 1:return depth;case 2:return 3;case 3:return sizeof(alarm::Widths)/sizeof(*alarm::Widths);case 4:return 5;case 5:return sizeof(alarm::Durations);default:return 0;}
}
inline uint8_t alarmValue(const alarm::Settings& s,unsigned row) {
    switch(row){case 0:return s.enabled;case 1:return s.level?s.level-1:0;case 2:return unsigned(s.mode);case 3:return s.width;case 4:return unsigned(s.tone);case 5:return s.duration;default:return 0;}
}
inline void setAlarmValue(alarm::Settings& s,unsigned row,uint8_t choice) {
    switch(row){case 0:s.enabled=choice!=0;break;case 1:s.level=choice+1;break;case 2:s.mode=static_cast<alarm::Mode>(choice);break;
        case 3:s.width=choice;break;case 4:s.tone=static_cast<alarm::Tone>(choice);break;case 5:s.duration=choice;break;default:break;}
}
inline const char* alarmLabel(unsigned row,bool ja) {
    static constexpr const char* en[]={"Enabled","Level","Mode","Width","Sound","Duration","Test sound","Reload"};
    static constexpr const char* jp[]={"通知","監視階層","判定方式","判定幅","通知音","通知時間","試聴","再読込"};
    return row<AlarmRows?(ja?jp[row]:en[row]):"--";
}
inline void alarmValueText(unsigned row,unsigned choice,unsigned depth,bool ja,char* out,size_t size) {
    const char* value=nullptr;
    switch(row) {
        case 0:value=choice?"ON":"OFF";break;
        case 1:std::snprintf(out,size,"%u/%u",choice+1,depth);return;
        case 2:{static constexpr const char* en[]={"Every","Relief","Confirm"};static constexpr const char* jp[]={"毎回","緩和","進入確定"};if(choice<3)value=ja?jp[choice]:en[choice];break;}
        case 3:if(choice<sizeof(alarm::Widths)/sizeof(*alarm::Widths)){std::snprintf(out,size,"%um",alarm::Widths[choice]);return;}break;
        case 4:{static constexpr const char* en[]={"Single","Double","Rising","Falling","Chime"};static constexpr const char* jp[]={"単音","二連音","上昇音","下降音","チャイム"};if(choice<5)value=ja?jp[choice]:en[choice];break;}
        case 5:if(choice<sizeof(alarm::Durations)) {const auto seconds=alarm::Durations[choice];if(seconds==0)value=ja?"1回":"Once";else if(seconds==255)value=ja?"停止まで":"Until stop";else {std::snprintf(out,size,"%us",seconds);return;}}break;
        default:value="";break;
    }
    std::snprintf(out,size,"%s",value?value:"--");
}
} // namespace geoputer

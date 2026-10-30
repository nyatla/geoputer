#include "Settings.h"
#include <cstring>

namespace geoputer {
bool validDatasetLanguage(const char* value, bool allow_empty) {
    if (!value) return false;
    return validDatasetLanguage(std::string_view(value, strnlen(value, config::DatasetLanguageBytes)), allow_empty);
}
bool validDatasetLanguage(std::string_view value, bool allow_empty) {
    const size_t length = value.size();
    if (!length) return allow_empty;
    if (length >= config::DatasetLanguageBytes || value[0] < 'a' || value[0] > 'z' || value=="dataset") return false;
    bool separator = false;
    for (size_t i=0;i<length;++i) {
        const char ch=value[i];
        if (ch=='-') { if (separator || i+1==length) return false; separator=true; }
        else { if (!((ch>='a' && ch<='z') || (ch>='0' && ch<='9'))) return false; separator=false; }
    }
    return true;
}
unsigned settingChoices(unsigned index) {
    constexpr unsigned counts[] = {2, 3, 5, 5, 5, 100, 5, 3, 2};
    return index < SettingCount ? counts[index] : 0;
}
uint8_t settingValue(const Settings& v, unsigned index) {
    switch(index) {
        case 0:return static_cast<uint8_t>(v.language); case 1:return v.log_interval;
        case 2:return v.flush_interval; case 3:return v.screen_timeout; case 4:return v.brightness;
        case 5:return v.dataset; case 6:return v.volume; case 7:return unsigned(v.power_level)-1; case 8:return v.gps_mode; default:return UINT8_MAX;
    }
}
bool setSettingValue(Settings& v,unsigned index,uint8_t choice) {
    if(choice>=settingChoices(index)) return false;
    switch(index) {
        case 0:v.language=static_cast<UiLanguage>(choice);break; case 1:v.log_interval=choice;break;
        case 2:v.flush_interval=choice;break;case 3:v.screen_timeout=choice;break;case 4:v.brightness=choice;break;
        case 5:v.dataset=choice;if(!choice) std::memset(v.wordbook_language,0,sizeof(v.wordbook_language));break;case 6:v.volume=choice;break;case 7:v.power_level=static_cast<power::Level>(choice+1);break;case 8:v.gps_mode=choice;break;default:return false;
    }
    return true;
}
bool validSettings(const Settings& v) {
    for(unsigned i=0;i<SettingCount;++i) if(settingValue(v,i)>=settingChoices(i)) return false;
    return validDatasetLanguage(v.wordbook_language,true) && (v.dataset || !v.wordbook_language[0]);
}
bool sameSettings(const Settings& a,const Settings& b) {
    for(unsigned i=0;i<SettingCount;++i) if(settingValue(a,i)!=settingValue(b,i)) return false;
    return !std::strncmp(a.wordbook_language,b.wordbook_language,config::DatasetLanguageBytes);
}
static uint32_t checksum(const uint8_t* bytes,size_t size) {
    uint32_t hash=2166136261u;
    for(size_t i=0;i<size;++i) hash=(hash^bytes[i])*16777619u;
    return hash;
}
void encodeSettings(const Settings& v,uint8_t (&record)[SettingsRecordSize]) {
    for(auto& byte:record) byte=0;
    record[0]='G';record[1]='P';record[2]=5;record[3]=SettingCount;
    for(unsigned i=0;i<5;++i) record[4+i]=settingValue(v,i+1);
    record[9]=v.gps_mode;record[11]=v.volume|((unsigned(v.power_level)-1)<<3);
    record[10]=static_cast<uint8_t>(v.language);
    std::memcpy(record+12,v.wordbook_language,config::DatasetLanguageBytes);
    const auto hash=checksum(record,SettingsRecordSize-4);
    for(unsigned i=0;i<4;++i) record[SettingsRecordSize-4+i]=static_cast<uint8_t>(hash>>(8*i));
}
bool decodeSettings(const uint8_t* record,size_t size,Settings& value) {
    if(!record || (size!=SettingsRecordSize && size!=LegacySettingsRecordSize) || record[0]!='G' || record[1]!='P' || (record[2]<4 && record[11])) return false;
    const bool old=size==LegacySettingsRecordSize;
    if(old ? !((record[2]==1 && record[3]==6 && record[10]==0) || (record[2]==2 && record[3]==7)) : !((record[2]==3 && record[3]==7) || (record[2]==4 && record[3]==8) || (record[2]==5 && record[3]==SettingCount))) return false;
    uint32_t hash=0;
    for(unsigned i=0;i<4;++i) hash|=static_cast<uint32_t>(record[size-4+i])<<(8*i);
    if(hash!=checksum(record,size-4)) return false;
    Settings candidate=defaultSettings();
    for(unsigned i=0;i<6;++i) {
        if(old && i==4) { if(record[8]>=3) return false; candidate.dataset=1; continue; } // Old values were unused mode indices, never folder IDs.
        if(!setSettingValue(candidate,i==5?static_cast<unsigned>(SettingId::GpsMode):i+1,record[4+i])) return false;
    }
    if(record[2]!=1 && !setSettingValue(candidate,0,record[10])) return false;
    if(!old) std::memcpy(candidate.wordbook_language,record+12,config::DatasetLanguageBytes);
    if(record[2]>=4) {
        if(record[2]==5 && (record[11]&0xe0))return false;
        if(!setSettingValue(candidate,static_cast<unsigned>(SettingId::Volume),record[2]==5?record[11]&7:record[11]))return false;
        if(record[2]==5 && !setSettingValue(candidate,static_cast<unsigned>(SettingId::PowerSaving),record[11]>>3))return false;
    }
    if(!validSettings(candidate)) return false;
    value=candidate;
    return true;
}
} // namespace geoputer

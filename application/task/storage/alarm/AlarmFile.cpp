#include "AlarmFile.h"
namespace geoputer {
namespace {
uint32_t crc(const uint8_t* p,size_t n) {uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&uint32_t(-int32_t(c&1)));}return ~c;}
void put(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=v>>(8*i);}
uint32_t get(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
}
void AlarmFile::encode(const alarm::Profile& p,uint8_t (&b)[Bytes]) {
    std::memset(b,0,sizeof(b));std::memcpy(b,"RGAL",4);b[4]=3;b[5]=Bytes;b[6]=p.dataset;b[7]=p.depth;
    put(b+8,p.identity);b[12]=p.settings.enabled;b[13]=0;b[14]=unsigned(p.settings.judgement);
    b[15]=0;b[16]=unsigned(p.settings.tone);b[17]=p.settings.duration;put(b+60,crc(b,60));
}
bool AlarmFile::decode(const uint8_t (&b)[Bytes],alarm::Profile& p) {
    if(std::memcmp(b,"RGAL",4)||(b[4]<1 || b[4]>3)||b[5]!=Bytes||b[6]!=p.dataset||b[7]!=p.depth||get(b+8)!=p.identity||b[12]>1||get(b+60)!=crc(b,60))return false;
    for(unsigned i=18;i<60;++i)if(b[i])return false;
    // Version 1 used the same three enum numbers plus a metre-width choice.
    if((b[4]==1 && b[15]>5) || (b[4]>=2 && b[15]!=0))return false;
    if(b[4]<3 ? (!b[13] || b[13]>p.depth) : b[13]!=0)return false;
    alarm::Settings s{b[12]!=0,static_cast<alarm::Judgement>(b[14]),static_cast<alarm::Tone>(b[16]),b[17]};
    if(!alarm::valid(s,p.depth))return false;
    p.settings=s;return true;
}
int AlarmFile::exists(FileReadPath p)const {
#ifdef ARDUINO_ARCH_ESP32
    FILINFO info{};const auto result=f_stat(storageNativePath(drive_,p).c_str(),&info);
    return result==FR_OK?((info.fattrib&AM_DIR)?-1:1):result==FR_NO_FILE?0:-1;
#else
    struct stat info{};return ::stat(p.resolve().c_str(),&info)==0?(S_ISREG(info.st_mode)?1:-1):errno==ENOENT?0:-1;
#endif
}
bool AlarmFile::remove(FileReadPath p)const {
    const int found=exists(p);if(found<0)return false;if(!found)return true;
#ifdef ARDUINO_ARCH_ESP32
    const auto result=f_unlink(storageNativePath(drive_,p).c_str());return result==FR_OK||result==FR_NO_FILE;
#else
    return std::remove(p.resolve().c_str())==0||errno==ENOENT;
#endif
}
bool AlarmFile::rename(FileReadPath a,FileReadPath b)const {
#ifdef ARDUINO_ARCH_ESP32
    return f_rename(storageNativePath(drive_,a).c_str(),storageNativePath(drive_,b).c_str())==FR_OK;
#else
    return std::rename(a.resolve().c_str(),b.resolve().c_str())==0;
#endif
}
alarm::Profile AlarmFile::load(uint8_t n,uint8_t depth,uint32_t identity) {
    SdAccessScope access(observer_);alarm::Profile p{};p.dataset=n;p.depth=depth;p.identity=identity;
    if(!n || !alarm::valid(p.settings,depth)) {p.state=alarm::State::DataError;return p;}
    const auto target=path(n,"_alert.dat");const int found=exists(target);
    if(found<0){p.state=alarm::State::IoError;return p;}
    bool corrupt=false,upgrade=false;
    if(found) {
        ReadFile file(drive_,&slot_);uint64_t bytes=0;uint8_t record[Bytes];
        if(!file.openRead(target)||!file.size(bytes)){p.state=alarm::State::IoError;return p;}
        if(bytes==Bytes) {
            if(!file.read(record,sizeof(record))){p.state=alarm::State::IoError;return p;}
            corrupt=!decode(record,p);upgrade=!corrupt && record[4]<3;
        } else corrupt=true;
        if(!file.close()){p.state=alarm::State::IoError;return p;}
        if(!corrupt){p.state=alarm::State::Ready;if(upgrade && !save(p))p.state=alarm::State::IoError;return p;}
        if(!remove(target)){p.state=alarm::State::IoError;return p;}
    }
    p.settings={};p.state=corrupt?alarm::State::Repaired:alarm::State::Created;
    if(!save(p))p.state=alarm::State::IoError;
    return p;
}
bool AlarmFile::save(const alarm::Profile& p) {
    SdAccessScope access(observer_);if(!p.dataset||!alarm::valid(p.settings,p.depth))return false;
    const auto target=path(p.dataset,"_alert.dat"),temp=path(p.dataset,"_alert.tmp"),backup=path(p.dataset,"_alert.bak");
    if(!remove(temp))return false;
    uint8_t record[Bytes];encode(p,record);
    {
        ReadFile file(drive_,&slot_);
        const bool written=file.openStaging(temp) && file.write(record,sizeof(record)) && file.sync();
        const bool closed=file.close();
        if(!written || !closed){remove(temp);return false;}
    }
    {
        ReadFile file(drive_,&slot_);uint8_t actual[Bytes];alarm::Profile check=p;
        const bool read=file.openRead(temp) && file.read(actual,sizeof(actual));
        const bool closed=file.close();
        if(!read || !closed || !decode(actual,check)){remove(temp);return false;}
    }
    const int found=exists(target);if(found<0||!remove(backup)){remove(temp);return false;}
    if(found&&!rename(target,backup)){remove(temp);return false;}
    if(!rename(temp,target)) {if(found)rename(backup,target);remove(temp);return false;}
    // A leftover backup is never auto-adopted at boot; a missing main means OFF.
    return remove(backup);
}
} // namespace geoputer

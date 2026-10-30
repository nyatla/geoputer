#include "WifiCredentials.h"
#include <Preferences.h>
#include <cstring>
namespace geoputer::wifi {
namespace {
struct Record {char magic[4],password[PasswordBytes];uint8_t checksum;};
enum class Read {Missing, Invalid, Failed, Valid};
Read read(Preferences& nvs,Record& record) {
    if(!nvs.isKey("ap"))return Read::Missing;
    if(nvs.getBytesLength("ap")!=sizeof(record))return Read::Invalid;
    if(nvs.getBytes("ap",&record,sizeof(record))!=sizeof(record))return Read::Failed;
    uint8_t sum=0;
    for(unsigned i=0;i<16;++i) {
        const auto c=record.password[i];if(!((c>='A' && c<='Z') || (c>='2' && c<='9')))return Read::Invalid;
        sum^=c;
    }
    return !std::memcmp(record.magic,"WAP1",4) && !record.password[16] && record.checksum==sum?Read::Valid:Read::Invalid;
}
bool generate(Preferences& nvs,const Record* old,char (&out)[PasswordBytes],RandomWord random) {
    if(!random)return false;
    constexpr char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    static_assert(sizeof(alphabet)==33,"Uniform 5-bit password alphabet");
    for(unsigned attempt=0;attempt<4;++attempt) {
        Record record{};std::memcpy(record.magic,"WAP1",4);
        for(unsigned i=0;i<16;++i){record.password[i]=alphabet[random()&31];record.checksum^=record.password[i];}
        if(old && !std::memcmp(record.password,old->password,PasswordBytes))continue;
        if(nvs.putBytes("ap",&record,sizeof(record))!=sizeof(record))return false;
        std::memcpy(out,record.password,PasswordBytes);return true;
    }
    return false;
}
}
bool loadOrCreatePassword(char (&out)[PasswordBytes],RandomWord random) {
    Preferences nvs;if(!nvs.begin("rgp_wifi",false))return false;
    Record record{};const auto result=read(nvs,record);
    if(result==Read::Valid){std::memcpy(out,record.password,PasswordBytes);return true;}
    // A read error must not replace an already saved password.
    if(result==Read::Failed)return false;
    return generate(nvs,nullptr,out,random);
}
bool regeneratePassword(char (&out)[PasswordBytes],RandomWord random) {
    Preferences nvs;if(!nvs.begin("rgp_wifi",false))return false;
    Record old{};if(read(nvs,old)!=Read::Valid)return false;
    return generate(nvs,&old,out,random);
}
}

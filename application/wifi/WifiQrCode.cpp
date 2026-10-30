#include "WifiQrCode.h"
#include <lgfx/utility/lgfx_qrcode.h>
#include <cstring>
namespace geoputer::wifi {
namespace {
bool append(char*& out,size_t& left,const char* value,bool escape=false) {
    while(*value) {
        const bool prefix=escape && std::strchr("\\;,:\"",*value);
        if(left<=size_t(1+prefix))return false;
        if(prefix){*out++='\\';--left;}
        *out++=*value++;--left;
    }
    *out=0;return true;
}
}
bool connectionPayload(const WifiView& view,char* out,size_t capacity) {
    if(!out || !capacity)return false;
    *out=0;
    if(!std::memchr(view.ssid,0,sizeof(view.ssid)) || !std::memchr(view.password,0,sizeof(view.password)) ||
       !view.ssid[0] || !view.password[0])return false;
    auto* p=out;auto left=capacity;
    return append(p,left,"WIFI:T:WPA;S:") && append(p,left,view.ssid,true) &&
        append(p,left,";P:") && append(p,left,view.password,true) && append(p,left,";;");
}
bool QrCode::update(const WifiView& view,WifiPage page) {
    char data[sizeof(payload_)]{};
    const bool network=page==WifiPage::ConnectionQr;
    const auto version=network?5:2;
    if(network) {
        if(!connectionPayload(view,data,sizeof(data))){size_=0;payload_[0]=0;return false;}
    } else {
        if(page!=WifiPage::WebQr || !std::memchr(view.url,0,sizeof(view.url)) || !view.url[0]){size_=0;payload_[0]=0;return false;}
        std::strcpy(data,view.url);
    }
    // Byte-mode capacities for the selected versions at ECC Medium. Guard
    // before entering the third-party encoder, whose workspace is fixed here.
    if(std::strlen(data)>(network?84u:26u)){size_=0;payload_[0]=0;return false;}
    if(size_ && version_==version && !std::strcmp(payload_,data))return true;
    QRCode qr{};
    if(lgfx_qrcode_getBufferSize(version)>modules_.size() ||
       lgfx_qrcode_initText(&qr,modules_.data(),version,ECC_MEDIUM,data)!=0){size_=0;payload_[0]=0;return false;}
    std::strcpy(payload_,data);size_=qr.size;version_=version;return true;
}
}

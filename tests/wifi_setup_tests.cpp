#include "wifi/WifiCredentials.h"
#include "wifi/WifiQrCode.h"
#include "wifi/WifiPortal.h"
#include <Preferences.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
using namespace geoputer;
namespace {
uint32_t sequence=0;
uint32_t randomWord(){return sequence++;}
uint32_t constantWord(){return 0;}
void image(const wifi::QrCode& code,const char* filename) {
    auto* f=std::fopen(filename,"wb");assert(f);std::fprintf(f,"P5\n100 100\n255\n");
    for(unsigned y=0;y<100;++y)for(unsigned x=0;x<100;++x) {
        const unsigned off=code.offset(),cell=code.cell();
        const bool dark=x>=off && y>=off && x<off+code.size()*cell && y<off+code.size()*cell && code.dark((x-off)/cell,(y-off)/cell);
        std::fputc(dark?0:255,f);
    }
    assert(!std::fclose(f));
}
}
int main(int argc,char** argv) {
    assert(argc==3);Preferences::records.clear();
    char first[17]="unchanged",out[17]{};
    assert(wifi::loadOrCreatePassword(first,randomWord) && std::strlen(first)==16);
    const auto writes=Preferences::write_calls;
    assert(wifi::loadOrCreatePassword(out,nullptr) && !std::strcmp(first,out) && writes==Preferences::write_calls);
    assert(wifi::regeneratePassword(out,randomWord) && std::strcmp(first,out));
    char persisted[17];std::strcpy(persisted,out);
    Preferences::fail_write=true;std::strcpy(out,"preserve");assert(!wifi::regeneratePassword(out,randomWord));
    assert(!std::strcmp(out,"preserve"));Preferences::fail_write=false;
    assert(wifi::loadOrCreatePassword(out,nullptr) && !std::strcmp(out,persisted));
    Preferences::fail_read=true;assert(!wifi::regeneratePassword(out,randomWord));assert(!wifi::loadOrCreatePassword(out,randomWord));Preferences::fail_read=false;
    Preferences::fail_open=true;assert(!wifi::loadOrCreatePassword(out,randomWord));Preferences::fail_open=false;
    Preferences::records.clear();assert(wifi::loadOrCreatePassword(out,constantWord));
    const auto before=Preferences::write_calls;assert(!wifi::regeneratePassword(out,constantWord) && before==Preferences::write_calls);
    WifiView view{};std::strcpy(view.ssid,"Geoputer-ABCD");std::strcpy(view.password,"ABCD2345EFGH6789");std::strcpy(view.url,"http://192.168.4.1/");
    wifi::QrCode qr;assert(qr.update(view,WifiPage::ConnectionQr));
    assert(!std::strcmp(qr.payload(),"WIFI:T:WPA;S:Geoputer-ABCD;P:ABCD2345EFGH6789;;"));
    assert(qr.size()==37 && qr.cell()==2 && qr.offset()>=4*qr.cell());image(qr,argv[1]);
    assert(qr.update(view,WifiPage::ConnectionQr)); // Cached update preserves the matrix.
    assert(qr.update(view,WifiPage::WebQr) && !std::strcmp(qr.payload(),view.url));
    assert(qr.size()==25 && qr.cell()==3 && qr.offset()>=4*qr.cell());image(qr,argv[2]);
    view.url[0]=0;assert(!qr.update(view,WifiPage::WebQr) && !qr.valid());
    std::strcpy(view.ssid,"A;B:C\\D,\"");char text[96];assert(wifi::connectionPayload(view,text,sizeof(text)));
    assert(std::strstr(text,"S:A\\;B\\:C\\\\D\\,\\\";P:"));assert(!wifi::connectionPayload(view,text,5));
    std::memset(view.ssid,'x',sizeof(view.ssid));assert(!wifi::connectionPayload(view,text,sizeof(text)));
    assert(wifi::canonicalHost("192.168.4.1","192.168.4.1"));assert(wifi::canonicalHost("192.168.4.1:80","192.168.4.1"));
    assert(!wifi::canonicalHost("192.168.4.1.evil","192.168.4.1"));assert(!wifi::canonicalHost("evil","192.168.4.1"));
    assert(wifi::captiveProbe("/generate_204") && wifi::captiveProbe("/hotspot-detect.html") && wifi::captiveProbe("/connecttest.txt"));
    assert(!wifi::captiveProbe("/") && !wifi::captiveProbe("/api/status"));
    std::puts("PASS Wi-Fi setup: persistent random credentials, regeneration, NVS failures, same-password rejection, bounded QR encoding/quiet zones, payload escaping and captive host/probe routing");
}

#if defined(ARDUINO_ARCH_ESP32)
#include "WifiService.h"
#include "WifiWeb.h"
#include "WifiCredentials.h"
#include "WifiPortal.h"
#include "../BootMode.h"
#include "../BuildInfo.h"
#include "../utils/Console.h"
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
namespace geoputer {
class WifiService::Handler final:public RequestHandler {
public:
    explicit Handler(WifiService& owner):owner_(owner){}
    // Catch every POST, including unknown routes, so the library never buffers
    // an arbitrary request body in a String. Bodies use its streaming raw API.
    bool canHandle(HTTPMethod,String)override{return true;}
    bool canRaw(String)override{return true;}
    bool handle(WebServer&,HTTPMethod,String)override{owner_.handle();return true;}
    void raw(WebServer&,String,HTTPRaw& body)override{owner_.raw(body);}
private:WifiService& owner_;
};
bool WifiService::startupCredentials() {
    if(!wifi::loadOrCreatePassword(view_.password,esp_random))return false;
    uint8_t mac[6];WiFi.macAddress(mac);
    std::snprintf(view_.ssid,sizeof(view_.ssid),"ReverseGeoputer-%02X%02X",mac[4],mac[5]);
    for(unsigned i=0;i<16;++i)std::snprintf(csrf_+i*2,3,"%02x",esp_random()&255);
    return true;
}
bool WifiService::begin(uint8_t drive,bool mounted,StorageObserver* observer,Changed changed,void* context) {
    observer_=observer;changed_=changed;context_=context;mounted_=mounted;files_.attach(drive);
    WiFi.persistent(false);
    init_heap_=ESP.getFreeHeap();init_largest_=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!WiFi.mode(WIFI_AP))view_.error=WifiError::RadioInit;
    else if(!startupCredentials())view_.error=WifiError::Credentials;
    else if(!WiFi.softAP(view_.ssid,view_.password,1,false,1))view_.error=WifiError::AccessPoint;
    if(view_.error!=WifiError::None) {
        WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);
        phase(WifiPhase::Failed,true);return false;
    }
    std::snprintf(view_.url,sizeof(view_.url),"http://%s/",WiFi.softAPIP().toString().c_str());
    if(mounted_) {SdAccessScope activity(observer_);if(!files_.recover()) {view_.error=WifiError::SdRecovery;mounted_=false;std::snprintf(upload_error_,sizeof(upload_error_),"%s",files_.error());}}
    const char* fields[]={"X-Session","X-Path","X-Replace","X-Version","Origin","Content-Type","Content-Length","Transfer-Encoding"};
    server_.collectHeaders(fields,sizeof(fields)/sizeof(*fields));
    server_.addHandler(new Handler(*this)); // One library-owned route, allocated once at mode startup.
    server_.begin();started_=true;
    dns_.setTTL(60);view_.portal=dns_.start(53,"*",WiFi.softAPIP());phase(WifiPhase::Ready,true);
    Console.printf("[WIFI] AP ready ssid=%s url=%s SD=%u heap=%u\n",view_.ssid,view_.url,mounted_,ESP.getFreeHeap());
    return true;
}
void WifiService::phase(WifiPhase state,bool force) {
    if(state==WifiPhase::Ready && !mounted_ && upload_error_[0])state=WifiPhase::Failed;
    const bool change=view_.phase!=state;view_.phase=state;
    const uint32_t now=millis();
    if(changed_ && (force || change || now-notified_at_>=150)){notified_at_=now;changed_(context_);}
}
void WifiService::step() {
    if(!startup_reported_ && millis()-error_reported_at_>=5000) {
        const auto written=Console.printf("[WIFI] startup=%s phase=%s heap_before=%lu largest_before=%lu heap_now=%u largest_now=%u stack_free=%u\n",
            view_.error==WifiError::None?"OK":wifiErrorName(view_.error),wifiPhaseName(view_.phase),
            static_cast<unsigned long>(init_heap_),static_cast<unsigned long>(init_largest_),ESP.getFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),uxTaskGetStackHighWaterMark(nullptr));
        startup_reported_=written>0;error_reported_at_=millis();
    }
    if(view_.restart_ready)return;
    if(exiting()) {
        phase(WifiPhase::Stopping); // Poll any last queued control before choosing the target.
        files_.abort();
        bool saved=false;
        if(exit_requested_)saved=saveBootMode(BootMode::GpsLogger);
        else {char next[wifi::PasswordBytes];saved=wifi::regeneratePassword(next,esp_random);}
        if(!saved) {
            exit_requested_=regenerate_requested_=false;
            view_.error=WifiError::Restart;
            std::strcpy(upload_error_,"Cannot save restart/password settings");
            phase(WifiPhase::Failed,true);return;
        }
        if(view_.portal)dns_.stop();
        view_.portal=false;
        if(started_)server_.stop();
        WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);started_=false;
        view_.restart_ready=true;phase(WifiPhase::Stopping,true);return;
    }
    if(!started_)return;
    const auto clients=WiFi.softAPgetStationNum();
    if(view_.clients!=clients){view_.clients=clients;phase(view_.phase,true);}
    if(view_.portal)dns_.processNextRequest();
    server_.handleClient();
}
void WifiService::headers() {
    server_.sendHeader("Cache-Control","no-store");
    server_.sendHeader("X-Content-Type-Options","nosniff");
    server_.sendHeader("X-Frame-Options","DENY");
    server_.sendHeader("Content-Security-Policy","default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
}
void WifiService::emit(const char* value){server_.sendContent(value,std::strlen(value));}
void WifiService::quoted(const char* value) {
    emit("\"");char buffer[96];size_t used=0;
    for(const auto* p=reinterpret_cast<const unsigned char*>(value);*p;++p) {
        if(used+7>=sizeof(buffer)){server_.sendContent(buffer,used);used=0;}
        if(*p=='"'||*p=='\\'){buffer[used++]='\\';buffer[used++]=*p;}
        else if(*p<32){std::snprintf(buffer+used,7,"\\u%04x",*p);used+=6;}
        else buffer[used++]=*p;
    }
    if(used)server_.sendContent(buffer,used);
    emit("\"");
}
void WifiService::response(int code,const char* error) {
    headers();server_.setContentLength(CONTENT_LENGTH_UNKNOWN);server_.send(code,"application/json; charset=utf-8","");
    emit(error?"{\"error\":":"{\"ok\":true");if(error)quoted(error);emit("}");server_.sendContent("");
}
bool WifiService::authorized() {
    // Custom header is unavailable to cross-origin forms. No CORS allowance.
    const auto host=server_.hostHeader(),ip=WiFi.softAPIP().toString();
    return server_.header("X-Session")==csrf_ && wifi::canonicalHost({host.c_str(),host.length()},{ip.c_str(),ip.length()}) &&
        (!server_.hasHeader("Origin") || server_.header("Origin")==String("http://")+WiFi.softAPIP().toString());
}
bool WifiService::path(char* out) {
    const auto encoded=server_.header("X-Path");
    return wifi::decodePath({encoded.c_str(),encoded.length()},out,256);
}
void WifiService::raw(HTTPRaw& body) {
    if(body.status==RAW_START) {
        body_started_=true;body_finished_=false;upload_ok_=false;upload_error_[0]=0;
        const auto uri=server_.uri();
        if(uri!="/api/upload" && uri!="/api/save") {
            body_started_=false;
            if(server_.clientContentLength()>0 || !authorized()){response(403,"Invalid or unauthorized request");server_.client().stop();}
            return;
        }
        if(!authorized())std::strcpy(upload_error_,"Unauthorized request");
        else if(exiting())std::strcpy(upload_error_,"Mode is stopping");
        else if(!mounted_)std::strcpy(upload_error_,"SD unavailable or recovery required");
        else if((uri!="/api/upload" && uri!="/api/save") || !path(path_))std::strcpy(upload_error_,"Invalid request path");
        else if(!server_.hasHeader("Content-Length") || server_.hasHeader("Transfer-Encoding") || server_.clientContentLength()<0)
            std::strcpy(upload_error_,"Fixed Content-Length required");
        else {
            SdAccessScope activity(observer_);const auto version=server_.header("X-Version");
            upload_ok_=files_.beginWrite(path_,static_cast<uint32_t>(server_.clientContentLength()),server_.header("X-Replace")=="1",
                uri=="/api/save"?version.c_str():nullptr,uri=="/api/save");
            if(!upload_ok_)std::snprintf(upload_error_,sizeof(upload_error_),"%s",files_.error());
        }
        view_.transferred=0;view_.total=static_cast<uint32_t>(std::max(0,server_.clientContentLength()));
        phase(WifiPhase::Receiving,true);
        if(!upload_ok_){response(409,upload_error_);server_.client().stop();} // Reject before reading a large/unknown body.
    } else if(body.status==RAW_WRITE && upload_ok_) {
        SdAccessScope activity(observer_);
        if(exiting() || !files_.write(body.buf,body.currentSize)) {
            upload_ok_=false;std::snprintf(upload_error_,sizeof(upload_error_),"%s",exiting()?"Cancelled":files_.error());
            files_.abort();server_.client().stop();
        } else {view_.transferred+=body.currentSize;phase(WifiPhase::Receiving);}
    } else if(body.status==RAW_END)body_finished_=true;
    else if(body.status==RAW_ABORTED) {
        SdAccessScope activity(observer_);files_.abort();upload_ok_=false;body_started_=body_finished_=false;phase(WifiPhase::Ready,true);
    }
}
bool WifiService::cancelDelete(void* context) {
    auto& self=*static_cast<WifiService*>(context);self.phase(WifiPhase::Removing);
    return self.exiting() || !self.server_.client().connected();
}
void WifiService::status() {
    if(mounted_) {
        SdAccessScope activity(observer_);char volume[260];files_.native("/",volume,sizeof(volume));DWORD free=0;FATFS* fs=nullptr;
        if(f_getfree(volume,&free,&fs)==FR_OK && fs)status_.free_bytes=uint64_t(free)*fs->csize*512;
    }
    headers();server_.setContentLength(CONTENT_LENGTH_UNKNOWN);server_.send(200,"application/json; charset=utf-8","");
    emit("{\"session\":");quoted(csrf_);emit(",\"version\":");quoted(ApplicationVersion);
    emit(",\"sd\":");quoted(cardStateName(status_.card_state));
    emit(",\"notice\":");quoted(upload_error_);emit(",\"ssid\":");quoted(view_.ssid);
    emit(",\"wordbookLanguage\":");quoted(status_.settings.wordbook_language);
    char extra[96];std::snprintf(extra,sizeof(extra),",\"screenTimeout\":%u,\"pageEntries\":%u,\"textMaxBytes\":%lu,\"uploadMaxBytes\":%lu",
        ScreenTimeouts[status_.settings.screen_timeout],config::WifiPageEntries,static_cast<unsigned long>(config::WifiTextMaxBytes),static_cast<unsigned long>(config::WifiUploadMaxBytes));emit(extra);
    char line[256];std::snprintf(line,sizeof(line),",\"usable\":%s,\"total\":%llu,\"free\":%llu,\"dataset\":%u,\"language\":%u,\"logInterval\":%u,\"flushInterval\":%u,\"brightness\":%u,\"volume\":%u,\"power\":%u}",
        mounted_?"true":"false",static_cast<unsigned long long>(status_.total_bytes),static_cast<unsigned long long>(status_.free_bytes),status_.settings.dataset,
        static_cast<unsigned>(status_.settings.language),LogIntervals[status_.settings.log_interval],FlushIntervals[status_.settings.flush_interval],Brightnesses[status_.settings.brightness],status_.settings.volume,static_cast<unsigned>(status_.settings.power_level));
    emit(line);server_.sendContent("");
}
void WifiService::list() {
    const auto arg=server_.arg("path");const auto offset_arg=server_.arg("offset");
    if(arg.length()!=std::strlen(arg.c_str()) || !wifi::validPath({arg.c_str(),arg.length()})){response(400,"Invalid directory path");return;}
    char* end=nullptr;const auto offset=std::strtoul(offset_arg.c_str(),&end,10);
    if(!offset_arg.length() || *end || offset>1000000){response(400,"Invalid page offset");return;}
    SdAccessScope activity(observer_);phase(WifiPhase::Reading);
    char native[260];
    if(!files_.native(arg.c_str(),native,sizeof(native))){response(400,"Invalid directory path");phase(WifiPhase::Ready);return;}
    FF_DIR dir{};
    if(f_opendir(&dir,native)!=FR_OK){response(404,"Cannot open directory");phase(WifiPhase::Ready);return;}
    headers();server_.setContentLength(CONTENT_LENGTH_UNKNOWN);server_.send(200,"application/json; charset=utf-8","");
    emit("{\"entries\":[");unsigned seen=0,count=0;bool more=false,ok=true;FILINFO info{};
    for(;;) {
        if(f_readdir(&dir,&info)!=FR_OK){ok=false;break;}
        if(!info.fname[0])break;
        if(!wifi::Files::visible(info.fname))continue;
        if(seen++<offset)continue;
        if(count==config::WifiPageEntries){more=true;break;}
        if(count++)emit(",");
        emit("{\"name\":");quoted(info.fname);
        char line[128];std::snprintf(line,sizeof(line),",\"directory\":%s,\"size\":%llu,\"date\":%u,\"time\":%u}",
            (info.fattrib&AM_DIR)?"true":"false",static_cast<unsigned long long>(info.fsize),info.fdate,info.ftime);emit(line);
    }
    ok=(f_closedir(&dir)==FR_OK)&&ok;emit("],\"more\":");emit(more?"true":"false");
    if(!ok)emit(",\"error\":\"SD directory read failed\"");
    emit("}");server_.sendContent("");phase(WifiPhase::Ready,true);
}
void WifiService::text() {
    const auto arg=server_.arg("path");
    if(arg.length()!=std::strlen(arg.c_str()) || !wifi::validPath({arg.c_str(),arg.length()},false)){response(400,"Invalid file path");return;}
    SdAccessScope activity(observer_);phase(WifiPhase::Reading);
    char version[17];
    if(!files_.inspectText(arg.c_str(),version) || !files_.openRead(arg.c_str())){response(415,files_.error());phase(WifiPhase::Ready);return;}
    headers();server_.sendHeader("X-File-Version",version);server_.setContentLength(files_.size());server_.send(200,"text/plain; charset=utf-8","");
    while(server_.client().connected()) {
        unsigned n=0;if(!files_.read(files_.buffer(),config::WifiTransferBytes,n) || !n)break;
        server_.sendContent(reinterpret_cast<const char*>(files_.buffer()),n);
    }
    files_.close();phase(WifiPhase::Ready,true);
}
void WifiService::handle() {
    const auto uri=server_.uri();const auto method=server_.method();
    if(method==HTTP_GET) {
        const auto host=server_.hostHeader(),ip=WiFi.softAPIP().toString();
        if(!wifi::canonicalHost({host.c_str(),host.length()},{ip.c_str(),ip.length()}) || wifi::captiveProbe({uri.c_str(),uri.length()})) {
            headers();server_.sendHeader("Location",String(view_.url)+"connect");
            server_.send(302,"text/plain","Open ReverseGeoputer connection guide");return;
        }
        if(uri=="/connect"){headers();server_.send_P(200,"text/html; charset=utf-8",wifi::PortalPage);return;}
    }
    if(method==HTTP_GET && uri=="/"){headers();server_.send_P(200,"text/html; charset=utf-8",wifi::WebPage);return;}
    if(method==HTTP_GET && uri=="/api/status"){status();return;}
    if(method==HTTP_POST && (uri=="/api/upload" || uri=="/api/save")) {
        SdAccessScope activity(observer_);phase(WifiPhase::Saving);
        const bool ok=authorized() && body_started_ && body_finished_ && upload_ok_ && !exiting() && files_.finish();
        if(!ok){if(!upload_error_[0])std::snprintf(upload_error_,sizeof(upload_error_),"%s",files_.error()[0]?files_.error():"Incomplete or unauthorized upload");files_.abort();}
        body_started_=body_finished_=upload_ok_=false;
        response(ok?200:409,ok?nullptr:upload_error_);phase(WifiPhase::Ready,true);return;
    }
    if(method==HTTP_POST && !authorized()){response(403,"Unauthorized request");return;}
    if(method==HTTP_POST && uri=="/api/exit") {response(200);exit_requested_=true;return;}
    if(!mounted_){response(503,"SD unavailable or recovery required");return;}
    if(method==HTTP_GET && uri=="/api/list"){list();return;}
    if(method==HTTP_GET && uri=="/api/info") {
        const auto arg=server_.arg("path");
        if(arg.length()!=std::strlen(arg.c_str()) || !wifi::validPath({arg.c_str(),arg.length()},false)){response(400,"Invalid file path");return;}
        SdAccessScope activity(observer_);FILINFO info{};const auto result=files_.stat(arg.c_str(),info);
        if(result!=FR_OK && result!=FR_NO_FILE && result!=FR_NO_PATH){response(500,"Cannot inspect file");return;}
        headers();server_.send(200,"application/json",result==FR_OK?(info.fattrib&AM_DIR?"{\"exists\":true,\"directory\":true}":"{\"exists\":true,\"directory\":false}"):"{\"exists\":false}");return;
    }
    if(method==HTTP_GET && uri=="/api/text"){text();return;}
    if(method==HTTP_POST && (uri=="/api/mkdir" || uri=="/api/delete")) {
        if(!path(path_) || server_.clientContentLength()>0){response(400,"Invalid request");return;}
        SdAccessScope activity(observer_);phase(uri=="/api/delete"?WifiPhase::Removing:WifiPhase::Saving);
        const bool ok=uri=="/api/mkdir"?files_.mkdir(path_):files_.remove(path_,cancelDelete,this);
        response(ok?200:409,ok?nullptr:files_.error());phase(WifiPhase::Ready,true);return;
    }
    response(404,"Unknown operation");
}
}
#endif

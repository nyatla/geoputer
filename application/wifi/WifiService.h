#pragma once
#include "WifiTypes.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "WifiFiles.h"
#include "../task/storage/StorageTypes.h"
#include "../task/storage/StorageActivity.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <atomic>
#endif
namespace geoputer {
// The Wi-Fi branch alone constructs this service. Its HTTP callbacks execute on
// Storage's loop, so SD ownership never crosses tasks. UI/App remain independent.
class WifiService {
public:
#if defined(ARDUINO_ARCH_ESP32)
    using Changed=void(*)(void*);
    bool begin(uint8_t drive,bool mounted,StorageObserver*,Changed,void*);
    void step();
    void requestExit(){exit_requested_.store(true);}
    bool exiting()const{return exit_requested_.load() || regenerate_requested_.load();}
    void requestRegenerate(){if(started_ && !exit_requested_)regenerate_requested_=true;}
    bool exitReady()const{return view_.restart_ready;}
    const WifiView& view()const{return view_;}
    void setStatus(const StorageSnapshot& status){status_=status;}
private:
    class Handler;
    bool authorized();
    bool path(char* out);
    void handle();
    void raw(HTTPRaw&);
    void list();
    void text();
    void status();
    void response(int code,const char* error=nullptr);
    void headers();
    void emit(const char*);
    void quoted(const char*);
    void phase(WifiPhase state,bool force=false);
    bool startupCredentials();
    static bool cancelDelete(void*);
    WebServer server_{80};
    DNSServer dns_;
    wifi::Files files_;
    WifiView view_{};
    StorageSnapshot status_{};
    StorageObserver* observer_=nullptr;
    Changed changed_=nullptr;
    void* context_=nullptr;
    std::atomic<bool> exit_requested_{false},regenerate_requested_{false};
    char csrf_[33]{},path_[256]{},upload_error_[128]{};
    uint32_t notified_at_=0,init_heap_=0,init_largest_=0;
    bool startup_reported_=false;
    uint32_t error_reported_at_=0;
    bool mounted_=false,started_=false,upload_ok_=false,body_started_=false,body_finished_=false;
#endif
};
}

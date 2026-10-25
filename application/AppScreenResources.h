#pragma once
#include "ScreenGroup.h"
#include "map/MapTracking.h"
#include <cassert>
#include <new>

namespace geoputer {
struct AppMapResources {
    map::FrameInfo info{};
    map::Transfer transfer{};
    map::TrackingAnchor anchor{};
    uint32_t retry_at=0;
    uint16_t row=0;
    bool ready=false,begun=false;
};
struct AppLogResources {
    LogReadRequest request{};
    uint8_t records[LogReadCount][32]{};
    uint32_t retry_at=0,cache_start=0;
    uint8_t cache_count=0;
    bool need=false,single=false;
};
// Only display-time working storage. Pending job identities live in App until
// their completion/cancellation is consumed, independently of this union.
class AppScreenResources {
public:
    void enter(ScreenGroup group) {
        const auto next=group==ScreenGroup::Map?Kind::Map:
            group==ScreenGroup::Logs || group==ScreenGroup::Usb?Kind::Logs:Kind::None;
        if(next==kind_)return;
        if(next==Kind::Map)new (&data_.map) AppMapResources{};
        if(next==Kind::Logs)new (&data_.logs) AppLogResources{};
        kind_=next;
    }
    bool hasMap() const {return kind_==Kind::Map;}
    bool hasLogs() const {return kind_==Kind::Logs;}
    AppMapResources& map() {assert(hasMap());return data_.map;}
    const AppMapResources& map() const {assert(hasMap());return data_.map;}
    AppLogResources& logs() {assert(hasLogs());return data_.logs;}
    const AppLogResources& logs() const {assert(hasLogs());return data_.logs;}
private:
    enum class Kind : uint8_t {None,Map,Logs};
    union Data {AppMapResources map;AppLogResources logs;constexpr Data():map{}{};} data_{};
    Kind kind_=Kind::None;
};
} // namespace geoputer

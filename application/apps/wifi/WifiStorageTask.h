#pragma once
#include "WifiMessages.h"
#include "../../utils/Task.h"
#include "../../task/storage/SettingsStore.h"
#include "../../wifi/WifiService.h"
#include <SPI.h>
namespace geoputer {
class WifiStorageTask final:public Task<WifiChannels::StorageRequest,1>,private StorageObserver {
public:
 WifiStorageTask(WifiChannels::Events& events,WifiChannels::Results& results):events_(events),results_(results){}
 const WifiState& state()const{return state_;}
protected:void run() override;
private:
 void handle(const WifiChannels::StorageRequest&);void step();void controls();void notify();
 void sdBegin()override;void sdEnd()override;void cacheState(RgCacheState,bool=false)override{}
 static void changed(void*);
 WifiChannels::Events& events_;WifiChannels::Results& results_;
 SettingsStore settings_;SPIClass spi_{FSPI};
 WifiState state_{};WifiChannels::Returned reply_{};
 uint8_t drive_=0xff;unsigned sd_depth_=0;bool initialized_=false,dirty_=false,reply_pending_=false;
 // File/HTTP owners close before observer state, SPI and settings are destroyed.
 WifiService service_;
};
}

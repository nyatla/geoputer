#pragma once
#include "StorageWorkArea.h"
namespace geoputer {
// Sessions follow the displayed map; the worker follows accepted jobs.
// StorageTask admits one read/map job, independently of the retained raster.
struct StorageScreenResources {
    explicit StorageScreenResources(map::CodeImageBuffer& pixels,StorageObserver* observer=nullptr)
        :live_map(pixels),log_map(pixels),work(observer){}
    map::MapSession live_map,log_map;
    map::MapSession& session(map::SessionId id) {return id==map::SessionId::Log?log_map:live_map;}
    size_t retainedMapBytes() const {return live_map.bytes()+log_map.bytes();}
    void clearMaps() {live_map.clear();log_map.clear();}
    void selectMap(map::SessionId id) {session(id==map::SessionId::Live?map::SessionId::Log:map::SessionId::Live).clear();}
    StorageWorkArea work;
};
} // namespace geoputer

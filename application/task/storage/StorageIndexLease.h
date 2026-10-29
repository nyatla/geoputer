#pragma once
#include "StorageMapMemory.h"
#include "StorageScreenResources.h"
#include <optional>

namespace geoputer {
// Synchronous Storage-only scope. Buffers/Readers die before the union changes.
class StorageIndexLease {
public:
    StorageIndexLease(StorageMapMemory& memory,StorageScreenResources& screens)
        :memory_(memory),screens_(screens) {
        if(memory.indexing() || screens.work.mapBusy() || screens.work.files().inUse())return;
        if(auto* logs=screens.work.activeLogs();logs && !logs->done())return;
        assert(screens.live_map.pixels==memory.image().data() && screens.log_map.pixels==memory.image().data());
        screens.live_map.unbind();screens.log_map.unbind();
        auto* scratch=memory.beginIndex();assert(scratch);
        work_.emplace(*scratch);
    }
    ~StorageIndexLease() {
        if(!work_)return;
        assert(!work_->inUse());work_.reset();memory_.endIndex();
        screens_.live_map.bind(memory_.image());screens_.log_map.bind(memory_.image());
    }
    StorageIndexLease(const StorageIndexLease&)=delete;
    StorageIndexLease& operator=(const StorageIndexLease&)=delete;
    explicit operator bool()const {return bool(work_);}
    DatasetIndexWork* work() {return work_?&*work_:nullptr;}
private:
    StorageMapMemory& memory_;
    StorageScreenResources& screens_;
    std::optional<DatasetIndexWork> work_;
};
} // namespace geoputer

#pragma once
#include <cstdint>

namespace geoputer {
enum class RgCacheState : uint8_t { Unprepared, Rebuilding, Ready };
struct RgCacheSnapshot {
    uint32_t revision = 0;
    uint32_t dataset_generation = 0;
    RgCacheState state = RgCacheState::Unprepared;
};
struct SdAccessSnapshot {
    uint32_t revision = 0;
    uint32_t access_id = 0;
    bool active = false;
};
inline bool newerRevision(uint32_t incoming, uint32_t current) {
    return incoming && (!current || static_cast<int32_t>(incoming - current) > 0);
}
template <typename Snapshot>
bool applyActivity(Snapshot& current, const Snapshot& incoming) {
    if (!newerRevision(incoming.revision, current.revision)) return false;
    current = incoming;
    return true;
}
class StorageObserver {
public:
    virtual ~StorageObserver() = default;
    virtual void sdBegin() = 0;
    virtual void sdEnd() = 0;
    virtual void cacheState(RgCacheState state, bool new_dataset = false) = 0;
};
class SdAccessScope {
public:
    explicit SdAccessScope(StorageObserver* observer) : observer_(observer) { if (observer_) observer_->sdBegin(); }
    ~SdAccessScope() { if (observer_) observer_->sdEnd(); }
    SdAccessScope(const SdAccessScope&) = delete;
    SdAccessScope& operator=(const SdAccessScope&) = delete;
private:
    StorageObserver* observer_;
};
}  // namespace geoputer

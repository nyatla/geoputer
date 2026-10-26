#pragma once
#include "../../../map/MapSession.h"
#include "../StorageActivity.h"
#include <memory>
#include <string>

namespace geoputer {
class BorrowedReadBuffer;
using MapSourceResult=map::MapSession;
// Independent reader session. Never accesses or invalidates CachedMapReader.
MapSourceResult readBoundaryMap(const std::string& directory, const std::string& language,
                                const map::Request& request, uint8_t drive, StorageObserver* observer,MapSourceResult reusable={},BorrowedReadBuffer* index_buffer=nullptr);
} // namespace geoputer

#pragma once
#include "../../../map/MapSession.h"
#include "../StorageActivity.h"
#include "../StoragePath.h"
#include "../dataset/SelectedDataset.h"

namespace geoputer {
class BorrowedReadBuffer;
class ReadFileSlot;
using MapSourceResult=map::MapSession;
// Independent reader session. Never accesses or invalidates CachedMapReader.
MapSourceResult readBoundaryMap(const SelectedDataset& selected,
                                const map::Request& request, uint8_t drive, StorageObserver* observer,MapSourceResult reusable,BorrowedReadBuffer* work_buffer=nullptr,ReadFileSlot* file_slot=nullptr,std::string_view root=config::DatasetRoot);
} // namespace geoputer

#pragma once
#include "../../Config.h"
#include "../../map/MapTypes.h"
#include "../../utils/BorrowedLayout.h"
#include "StoragePath.h"
#include <array>
#include <cstddef>

namespace geoputer {
using ReaderWorkspaceBytes=std::array<uint8_t,config::IndexReadBufferBytes>;
// Active only during map I/O. The UI back becomes packed boundary pixels after
// this object is destroyed. Neither array is zero-filled on construction.
struct MapReadWorkspace {
    ReaderWorkspaceBytes reader;
    std::array<char,config::MapNativePathBytes> path;
    NativePathBuffer nativePath() {return {path.data(),path.size()};}
};
static_assert(offsetof(MapReadWorkspace,path)==sizeof(ReaderWorkspaceBytes),"Reader and path scratch must be disjoint");
static_assert(sizeof(MapReadWorkspace)<=map::BoundaryBytes,"Map scratch must fit the UI back image");
} // namespace geoputer

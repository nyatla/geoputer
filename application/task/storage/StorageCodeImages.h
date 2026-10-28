#pragma once
#include "galuchat/CachedMapReader.h"
#include "../../map/MapSession.h"
namespace geoputer {
// Lifetime of StorageTask (the global Application). No array lives on a task
// stack, and no reader/session owns or frees either array.
struct StorageCodeImages {
    CodeCacheBuffer point{};
    map::CodeImageBuffer map{};
    StorageCodeImages()=default;
    StorageCodeImages(const StorageCodeImages&)=delete;
    StorageCodeImages& operator=(const StorageCodeImages&)=delete;
};
static_assert(sizeof(StorageCodeImages)==sizeof(CodeCacheBuffer)+config::MapViewCodeMaxBytes,"Two independent static code images");
} // namespace geoputer

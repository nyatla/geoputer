#pragma once
#include "galuchat/CachedMapReader.h"
#include "StorageMapMemory.h"
namespace geoputer {
// Lifetime of StorageTask (the global Application). No array lives on a task
// stack. A scoped lease controls the inner map/index union lifetime.
struct StorageCodeImages {
    CodeCacheBuffer point{};
    StorageMapMemory map;
    StorageCodeImages()=default;
    StorageCodeImages(const StorageCodeImages&)=delete;
    StorageCodeImages& operator=(const StorageCodeImages&)=delete;
};
static_assert(sizeof(StorageCodeImages)==sizeof(CodeCacheBuffer)+sizeof(StorageMapMemory),"Point cache plus exclusive map/index reservation");
} // namespace geoputer

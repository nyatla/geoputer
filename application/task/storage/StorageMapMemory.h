#pragma once
#include "../../map/MapSession.h"
#include "dataset/DatasetIndexWorkspace.h"
#include <cassert>
#include <new>

namespace geoputer {
// One static reservation: map pixels OR maintenance scratch, never both.
class StorageMapMemory {
public:
    using CodeImageBuffer=map::CodeImageBuffer;
    StorageMapMemory()=default;
    ~StorageMapMemory() {assert(!indexing_);}
    StorageMapMemory(const StorageMapMemory&)=delete;
    StorageMapMemory& operator=(const StorageMapMemory&)=delete;
    bool indexing()const {return indexing_;}
    map::CodeImageBuffer& image() {assert(!indexing_);return data_.map;}
private:
    friend class StorageIndexLease;
    DatasetIndexWorkspace* beginIndex() {
        if(indexing_)return nullptr;
        data_.map.~CodeImageBuffer();
        auto* result=::new(static_cast<void*>(&data_.index)) DatasetIndexWorkspace;
        indexing_=true;return result;
    }
    void endIndex() {
        assert(indexing_);data_.index.~DatasetIndexWorkspace();
        ::new(static_cast<void*>(&data_.map)) map::CodeImageBuffer;
        indexing_=false;
    }
private:
    union Data {
        map::CodeImageBuffer map;
        DatasetIndexWorkspace index;
        Data():map{} {}
        ~Data() {}
    } data_;
    bool indexing_=false;
};
} // namespace geoputer

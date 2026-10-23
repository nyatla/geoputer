#pragma once
#include "DatasetMetadata.h"
#include <array>
#include <stdexcept>

namespace geoputer {
struct FileIoTiming;
struct DatasetMapIndex {
    std::array<uint8_t,64> header{};
    int32_t west=0,south=0,width=0,height=0;
};
struct DatasetWordBookIndex {
    std::array<uint8_t,128> header{};
    uint32_t section_offset=0;
};
class MapChunkLimitExceeded : public std::runtime_error {
public:
    MapChunkLimitExceeded(uint32_t count,uint32_t limit)
        :std::runtime_error("Map chunk count exceeds limit"),count(count),limit(limit) {}
    const uint32_t count,limit;
};
class DatasetIndex {
public:
    using SaveSelection = bool (*)(void* context);
    explicit DatasetIndex(uint8_t drive=0, StorageObserver* observer=nullptr,
                          IndexProgressCallback progress=nullptr,void* context=nullptr)
        : progress_(progress), context_(context), drive_(drive), observer_(observer) {}
    // Writes and validates .tmp, publishes .dat, then saves selection. On save failure
    // restores the previous .dat. No automatic adoption of .tmp/.bak files.
    DatasetError apply(const std::string& directory, const DatasetMetadata& metadata,
                       SaveSelection save, void* context) const;
    // Explicit maintenance; never changes the selected dataset/language in NVS.
    DatasetError rebuild(const std::string& directory,const DatasetMetadata& metadata,
                         IndexTarget target=IndexTarget::All) const;
    DatasetError verifyAgainstDataset(const std::string& directory,const DatasetMetadata& metadata,
                                     IndexTarget target=IndexTarget::All) const;
    // Forward-only structural / CRC validation. Does not load a table into RAM.
    bool validate(const std::string& path) const;
    // Boot / explicit check: full index CRC + structure and GLC identity.
    DatasetMapIndex loadMap(const std::string& index,const std::string& map,
                            FileIoTiming* map_timing=nullptr,FileIoTiming* index_timing=nullptr,
                            DatasetWordBookIndex* wordbook=nullptr,const std::string& language="") const;
    // Verify the selected Wordbook fingerprint, chunk layout and common headers.
    // Returns GW00 metadata; no page/root index is retained or generated here.
    std::string checkWordBook(const DatasetWordBookIndex& index,const std::string& book,
                              FileIoTiming* timing=nullptr) const;
    // Candidate validation before an index exists. Retains no chunk array.
    DatasetMapIndex inspectMap(const std::string& map) const;
private:
    DatasetError build(const std::string&,const DatasetMetadata&,IndexTarget,SaveSelection,void*) const;
    IndexProgressCallback progress_;
    void* context_;
    uint8_t drive_;
    StorageObserver* observer_;
};
} // namespace geoputer

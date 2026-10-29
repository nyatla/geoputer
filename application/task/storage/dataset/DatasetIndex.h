#pragma once
#include <string_view>
#include "DatasetMetadata.h"
#include "DatasetIndexFormat.h"
#include "DatasetIndexWorkspace.h"
#include <array>
#include <stdexcept>

namespace geoputer {
struct FileIoTiming;
class BorrowedReadBuffer;
class ReadFileSlot;
class ReadFileSlots;
struct DatasetMapIndex {
    std::array<uint8_t,index_format::Header::Bytes> header{};
    int32_t west=0,south=0,width=0,height=0;
};
struct DatasetWordBookIndex {
    std::array<uint8_t,index_format::Wordbook::Bytes> header{};
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
    // Every operation requires caller-owned FIL slots and work memory.
    // loadMap may receive an explicit single slot/buffer instead; no owned fallback.
    explicit DatasetIndex(uint8_t drive=0, StorageObserver* observer=nullptr,
                          IndexProgressCallback progress=nullptr,void* context=nullptr,ReadFileSlots* files=nullptr,DatasetIndexWork* work=nullptr)
        : progress_(progress), context_(context), drive_(drive), observer_(observer),files_(files),work_(work) {}
    // Adopt an entirely current index without writing it; otherwise regenerate
    // all sections in .tmp, validate and publish. Save failure rolls back .dat.
    // Never partially reuse sections or automatically adopt .tmp/.bak files.
    DatasetError apply(std::string_view directory, const DatasetMetadata& metadata,
                       SaveSelection save, void* context) const;
    // Explicit maintenance; never changes the selected dataset/language in NVS.
    DatasetError rebuild(std::string_view directory,const DatasetMetadata& metadata) const;
    DatasetError verifyAgainstDataset(std::string_view directory,const DatasetMetadata& metadata) const;
    // Forward-only structural / CRC validation. Does not load a table into RAM.
    bool validate(std::string_view path) const;
    // Full index CRC + structure and GLC identity. A map request may lend
    // one buffer: the map-header and index Sources use it sequentially.
    DatasetMapIndex loadMap(std::string_view index,std::string_view map,
                            FileIoTiming* map_timing=nullptr,FileIoTiming* index_timing=nullptr,
                            DatasetWordBookIndex* wordbook=nullptr,std::string_view language="",
                            BorrowedReadBuffer* work=nullptr,ReadFileSlot* file_slot=nullptr) const;
    // Verify the selected Wordbook fingerprint, chunk layout and common headers.
    // GW00 metadata is opaque: validate its length, skip it, and return no text.
    void checkWordBook(const DatasetWordBookIndex& index,std::string_view book,
                              FileIoTiming* timing=nullptr,BorrowedReadBuffer* work=nullptr) const;
    // Candidate validation before an index exists. Retains no chunk array.
    DatasetMapIndex inspectMap(std::string_view map) const;
    // Candidate standard-header checks without an existing index or metadata copy.
    void inspectWordBook(std::string_view path) const;
private:
    DatasetError build(std::string_view,const DatasetMetadata&,SaveSelection,void*) const;
    IndexProgressCallback progress_;
    void* context_;
    uint8_t drive_;
    StorageObserver* observer_;
    ReadFileSlots* files_;
    DatasetIndexWork* work_; // Borrowed for one complete synchronous maintenance call.
};
} // namespace geoputer

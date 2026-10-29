#pragma once
#include "../../../Config.h"
#include "../../../utils/FileReadBuffer.h"
#include <array>
#include <cassert>
#include <cstdint>

namespace geoputer {
struct IndexChunkOrigin {int32_t x,y;};
struct IndexSourceStamp {
    uint32_t bytes=0,mtime=0,header_crc=0;
    bool operator==(const IndexSourceStamp& rhs)const {
        return bytes==rhs.bytes && mtime==rhs.mtime && header_crc==rhs.header_crc;
    }
};
struct IndexBookSource {
    char language[config::DatasetLanguageBytes],name[config::DatasetFilenameBytes];
    IndexSourceStamp stamp;
};
struct IndexBookTable {
    std::array<IndexBookSource,config::MaxDatasetLanguages> records;
    uint8_t count=0;
    size_t size()const {return count;}
    const IndexBookSource* begin()const {return records.data();}
    const IndexBookSource* end()const {return begin()+count;}
};
static_assert(sizeof(IndexSourceStamp)==12 && sizeof(IndexBookSource)==172,"Fixed source identification record");
// Trivial storage only. The owner chooses its lifetime and supplies a session.
struct DatasetIndexWorkspace {
    std::array<uint8_t,config::IndexReadBufferBytes> first,second;
    std::array<IndexChunkOrigin,config::MaxDatasetChunks> origins;
    IndexBookTable books;
};
static_assert(sizeof(IndexChunkOrigin)==8,"Two 32-bit coordinates per chunk");
static_assert(sizeof(DatasetIndexWorkspace)<=config::MapViewCodeMaxBytes,"Index work must fit the map image reservation");
class DatasetIndexWork {
public:
    explicit DatasetIndexWork(DatasetIndexWorkspace& memory)
        :memory_(memory),first_(memory.first.data(),memory.first.size()),second_(memory.second.data(),memory.second.size()) {}
    ~DatasetIndexWork() {assert(!books_used_);}
    DatasetIndexWork(const DatasetIndexWork&)=delete;
    DatasetIndexWork& operator=(const DatasetIndexWork&)=delete;
    bool inUse()const {return first_.inUse() || second_.inUse() || books_used_;}
    BorrowedReadBuffer* available() {
        if(!first_.inUse())return &first_;
        if(!second_.inUse())return &second_;
        throw std::logic_error("Index work buffers exhausted");
    }
    auto& origins() {return memory_.origins;}
    IndexBookTable& borrowBooks() {
        if(books_used_)throw std::logic_error("Index book table already borrowed");
        memory_.books.count=0;books_used_=true;return memory_.books;
    }
    void returnBooks(IndexBookTable& books) {
        assert(books_used_ && &books==&memory_.books);books_used_=false;
    }
private:
    DatasetIndexWorkspace& memory_;
    BorrowedReadBuffer first_,second_;
    bool books_used_=false;
};
} // namespace geoputer

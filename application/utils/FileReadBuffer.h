#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace geoputer {
class FileReadBuffer;
// Single Storage-task owner; the owner outlives every Reader that borrows it.
class BorrowedReadBuffer {
public:
    BorrowedReadBuffer(uint8_t* data,size_t capacity):data_(data),capacity_(capacity) {
        if(!data || !capacity)throw std::invalid_argument("empty borrowed read buffer");
    }
    ~BorrowedReadBuffer() {assert(!used_);}
    BorrowedReadBuffer(const BorrowedReadBuffer&)=delete;
    BorrowedReadBuffer& operator=(const BorrowedReadBuffer&)=delete;
    bool inUse() const {return used_;}
private:
    friend class FileReadBuffer;
    uint8_t* acquire(size_t bytes) {
        if(!bytes || bytes>capacity_)throw std::invalid_argument("borrowed read buffer too small");
        if(used_)throw std::logic_error("borrowed read buffer is already in use");
        used_=true;return data_;
    }
    void release() {assert(used_);used_=false;}
    uint8_t* data_;
    size_t capacity_;
    bool used_=false;
};
// Own a vector, or hold a non-owning lease. Destruction never frees borrowed RAM.
class FileReadBuffer {
public:
    FileReadBuffer()=default;
    ~FileReadBuffer() {if(borrowed_)borrowed_->release();}
    FileReadBuffer(const FileReadBuffer&)=delete;
    FileReadBuffer& operator=(const FileReadBuffer&)=delete;
    void resize(size_t bytes,BorrowedReadBuffer* borrowed=nullptr) {
        if(data_ || !bytes)throw std::invalid_argument("invalid read buffer initialization");
        if(borrowed) {data_=borrowed->acquire(bytes);borrowed_=borrowed;}
        else {owned_.resize(bytes);data_=owned_.data();}
        bytes_=bytes;
    }
    size_t size() const {return bytes_;}
    uint8_t* data() {return data_;}
    uint8_t& operator[](size_t offset) {return data_[offset];}
private:
    std::vector<uint8_t> owned_;
    BorrowedReadBuffer* borrowed_=nullptr;
    uint8_t* data_=nullptr;
    size_t bytes_=0;
};
} // namespace geoputer

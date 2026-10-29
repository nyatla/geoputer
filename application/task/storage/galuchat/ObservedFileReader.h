#pragma once
#include <algorithm>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#ifdef ARDUINO_ARCH_ESP32
#include "../StorageFs.h"
#else
#include <sys/stat.h>
#endif
#include <galuchat/wgsmap3_reader.hpp>
#include "../StorageActivity.h"
#include "ReadTiming.h"
#include "../StoragePath.h"
#include "../ReadFile.h"
#include "../../../Config.h"
#include "../../../utils/FileReadBuffer.h"

namespace geoputer {
struct FileReadOptions {
    size_t buffer_bytes = config::FileReadBufferBytes;
    size_t read_through_skip_bytes = config::FileReadThroughSkipBytes;
    void validate() const {
        if (!buffer_bytes) throw std::invalid_argument("file read buffer must be nonempty");
    }
};

// ESP32 accesses the already mounted FatFs volume directly; host tests retain
// stdio. Observe buffered operations, not each byte.
class ObservedFileReader final : public galuchat::ByteReader {
public:
    ObservedFileReader(FileReadPath path, size_t source_size, size_t offset, StorageObserver* observer,
                       FileIoTiming* timing = nullptr, FileReadOptions options = {}, uint8_t drive = 0, BorrowedReadBuffer* borrowed = nullptr, ReadFileSlot* file_slot = nullptr)
        : observer_(observer), timing_(diagnosticTiming(timing)), file_(drive,file_slot), origin_(offset),
          read_through_skip_bytes_(options.read_through_skip_bytes) {
        options.validate();
        if (offset > source_size) throw std::runtime_error("invalid reader offset");
        buffer_.resize(options.buffer_bytes,borrowed);
        SdAccessScope access(observer_);
        {
            FileIoTime elapsed(timing_, &FileIoTiming::open_us);
            if (timing_) ++timing_->opens;
            opened_ = file_.openRead(path);
        }
        if (!opened_) throw std::runtime_error("cannot open: " + std::string(path.resolve().view()));
        bool sought;
        {
            FileIoTime elapsed(timing_, &FileIoTiming::seek_us);
            if (timing_) ++timing_->seeks;
            sought = seekAbsolute(offset);
        }
        if (!sought) {
            FileIoTime elapsed(timing_, &FileIoTiming::close_us);
            if (timing_) ++timing_->closes;
            closeFile();
            throw std::runtime_error("cannot seek: " + std::string(path.resolve().view()));
        }
        length_ = source_size - offset;
    }
    ~ObservedFileReader() override {
        if (opened_) {
            SdAccessScope access(observer_);
            FileIoTime elapsed(timing_, &FileIoTiming::close_us);
            if (timing_) ++timing_->closes;
            closeFile();
        }
    }
    ObservedFileReader(const ObservedFileReader&) = delete;
    ObservedFileReader& operator=(const ObservedFileReader&) = delete;
    size_t pos() const override { return pos_; }
    bool atEnd() const override { return pos_ == length_; }
protected:
    uint8_t nextByte() override {
        if (pos_ >= length_) throw std::runtime_error("unexpected end of file");
        if (buffer_pos_ == buffer_end_) refillBuffer();
        ++pos_;
        return buffer_[buffer_pos_++];
    }
    void skipByte(size_t length) override {
        if (length > length_ - pos_) throw std::runtime_error("unexpected end of file");
        const auto buffered = buffer_end_ - buffer_pos_;
        if (length <= buffered) { buffer_pos_ += length; pos_ += length; return; }
        auto seek = length - buffered;
        if (seek <= read_through_skip_bytes_) {
            // Consume the existing tail, then discard whole buffered ranges.
            // Keep any read-ahead past the target for subsequent reads.
            buffer_pos_ = buffer_end_;
            pos_ += buffered;
            while (seek) {
                refillBuffer();
                const auto consumed = std::min(seek, buffer_end_);
                buffer_pos_ += consumed;
                pos_ += consumed;
                seek -= consumed;
            }
            return;
        }
        SdAccessScope access(observer_);
        bool sought;
        {
            FileIoTime elapsed(timing_, &FileIoTiming::seek_us);
            if (timing_) ++timing_->seeks;
            // Logical position excludes unread bytes in our own buffer.
            // FatFs seeks are absolute, including offsets above INT32_MAX.
            sought = seekAbsolute(origin_ + pos_ + length);
        }
        if (!sought) throw std::runtime_error("SD seek failed");
        pos_ += length; buffer_pos_ = buffer_end_ = 0;
    }
private:
    bool seekAbsolute(size_t offset) { return file_.seek(offset); }
    void closeFile() { file_.close(); opened_=false; }
    void refillBuffer() {
        SdAccessScope access(observer_);
        {
            FileIoTime elapsed(timing_, &FileIoTiming::read_us);
            if (timing_) ++timing_->reads;
            const auto count = std::min(buffer_.size(), length_ - pos_);
            const bool ok=file_.read(buffer_.data(),count,buffer_end_);
            if (timing_) timing_->bytes += buffer_end_;
            if (!ok) throw std::runtime_error("SD read failed");
        }
        buffer_pos_ = 0;
        if (!buffer_end_) throw std::runtime_error("SD read failed");
    }
    StorageObserver* observer_;
    FileIoTiming* timing_;
    // Construct/unwind in buffer -> file / file -> buffer order.
    FileReadBuffer buffer_;
    ReadFile file_;
    bool opened_ = false;
    size_t origin_ = 0, length_ = 0, pos_ = 0, buffer_pos_ = 0, buffer_end_ = 0;
    const size_t read_through_skip_bytes_;
};
// Storage-owned file description. It owns no path, FIL, read buffer or Reader.
// The path and native slot must outlive every scoped read using this description.
class FileReadSource {
public:
    FileReadSource(FileReadPath path,StorageObserver* observer,FileIoTiming* timing=nullptr,
                   FileReadOptions options={},uint8_t drive=0,ReadFileSlot* file_slot=nullptr)
        :path_(path),observer_(observer),timing_(diagnosticTiming(timing)),options_(options),
         drive_(drive),file_slot_(file_slot) {inspectSource();}
    FileReadSource(std::string&&,StorageObserver*,FileIoTiming* =nullptr,FileReadOptions ={},
                   uint8_t =0,ReadFileSlot* =nullptr)=delete;
    template<class Consumer>
    void withRange(size_t offset,size_t length,Consumer&& consume,BorrowedReadBuffer* work) const {
        if(offset>size_ || length>size_-offset)throw std::runtime_error("invalid reader range");
        if(!length) {galuchat::BufferReader empty(nullptr,0);consume(empty);return;}
        auto options=options_;options.buffer_bytes=std::min(options.buffer_bytes,length);
        ObservedFileReader reader(path_,offset+length,offset,observer_,timing_,options,drive_,work,file_slot_);
        consume(reader);
    }
private:
    void inspectSource() {
        options_.validate();
        SdAccessScope access(observer_);
#ifdef ARDUINO_ARCH_ESP32
        FILINFO status{};
        FRESULT stat_result;
        {
            FileIoTime elapsed(timing_, &FileIoTiming::stat_us);
            if (timing_) ++timing_->stats;
            stat_result=f_stat(fatFsPath(drive_,path_).c_str(),&status);
        }
        if (stat_result != FR_OK || (status.fattrib & AM_DIR)) throw std::runtime_error("cannot stat: " + std::string(path_.resolve().view()));
        size_ = status.fsize;
#else
        struct stat status{};
        int stat_result;
        {
            FileIoTime elapsed(timing_, &FileIoTiming::stat_us);
            if (timing_) ++timing_->stats;
            stat_result = stat(path_.resolve().c_str(), &status);
        }
        if (stat_result || status.st_size < 0) throw std::runtime_error("cannot stat: " + std::string(path_.resolve().view()));
        size_ = static_cast<size_t>(status.st_size);
#endif
    }
    FileReadPath path_;
    StorageObserver* observer_;
    FileIoTiming* timing_;
    size_t size_=0;
    FileReadOptions options_;
    uint8_t drive_;
    ReadFileSlot* file_slot_;
};

} // namespace geoputer

#pragma once
#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef ARDUINO_ARCH_ESP32
#include <ff.h>
#else
#include <sys/stat.h>
#endif
#include <galuchat/wgsmap3_reader.hpp>
#include "../StorageActivity.h"
#include "ReadTiming.h"
#include "../StoragePath.h"
#include "../../../Config.h"

namespace geoputer {
struct FileReadOptions {
    size_t buffer_bytes = config::FileReadBufferBytes;
    size_t read_through_skip_bytes = config::FileReadThroughSkipBytes;
    void validate() const {
        if (!buffer_bytes) throw std::invalid_argument("file read buffer must be nonempty");
    }
};

// SDK-compatible ReaderFactory. ESP32 accesses the already mounted FatFs volume
// directly; host tests retain stdio. Observe buffered operations, not each byte.
class ObservedFileReader final : public galuchat::ByteReader {
public:
    ObservedFileReader(const std::string& path, size_t source_size, size_t offset, StorageObserver* observer,
                       FileIoTiming* timing = nullptr, FileReadOptions options = {}, uint8_t drive = 0)
        : observer_(observer), timing_(diagnosticTiming(timing)), origin_(offset),
          read_through_skip_bytes_(options.read_through_skip_bytes) {
        options.validate();
        if (offset > source_size) throw std::runtime_error("invalid reader offset");
        buffer_.resize(options.buffer_bytes);
        SdAccessScope access(observer_);
        {
            FileIoTime elapsed(timing_, &FileIoTiming::open_us);
            if (timing_) ++timing_->opens;
#ifdef ARDUINO_ARCH_ESP32
            // Keep the Reader object small and allocate its large read buffer
            // before FIL. FIL includes a private sector cache on this SDK.
            file_ = std::make_unique<FIL>();
            opened_ = f_open(file_.get(), fatFsPath(drive, path).c_str(), FA_READ) == FR_OK;
#else
            (void)drive;
            file_ = std::fopen(path.c_str(), "rb");
            opened_ = file_ != nullptr;
#endif
        }
        if (!opened_) throw std::runtime_error("cannot open: " + path);
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
            throw std::runtime_error("cannot seek: " + path);
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
    bool seekAbsolute(size_t offset) {
#ifdef ARDUINO_ARCH_ESP32
        return offset <= std::numeric_limits<FSIZE_t>::max()
            && f_lseek(file_.get(), static_cast<FSIZE_t>(offset)) == FR_OK
            && f_tell(file_.get()) == offset; // A read-only seek can silently clip at EOF.
#else
        return offset <= static_cast<size_t>(std::numeric_limits<long>::max())
            && std::fseek(file_, static_cast<long>(offset), SEEK_SET) == 0;
#endif
    }
    void closeFile() {
#ifdef ARDUINO_ARCH_ESP32
        f_close(file_.get()); file_.reset();
#else
        std::fclose(file_); file_ = nullptr;
#endif
        opened_ = false;
    }
    void refillBuffer() {
        SdAccessScope access(observer_);
        {
            FileIoTime elapsed(timing_, &FileIoTiming::read_us);
            if (timing_) ++timing_->reads;
            const auto count = std::min(buffer_.size(), length_ - pos_);
#ifdef ARDUINO_ARCH_ESP32
            UINT received = 0;
            const auto result = f_read(file_.get(), buffer_.data(), static_cast<UINT>(count), &received);
            buffer_end_ = received;
            if (timing_) timing_->bytes += buffer_end_;
            if (result != FR_OK) throw std::runtime_error("SD read failed");
#else
            buffer_end_ = std::fread(buffer_.data(), 1, count, file_);
            if (timing_) timing_->bytes += buffer_end_;
            if (std::ferror(file_)) throw std::runtime_error("SD read failed");
#endif
        }
        buffer_pos_ = 0;
        if (!buffer_end_) throw std::runtime_error("SD read failed");
    }
    StorageObserver* observer_;
    FileIoTiming* timing_;
#ifdef ARDUINO_ARCH_ESP32
    std::unique_ptr<FIL> file_;
#else
    std::FILE* file_ = nullptr;
#endif
    bool opened_ = false;
    std::vector<uint8_t> buffer_;
    size_t origin_ = 0, length_ = 0, pos_ = 0, buffer_pos_ = 0, buffer_end_ = 0;
    const size_t read_through_skip_bytes_;
};
class ObservedFileReaderFactory final : public galuchat::ReaderFactory {
public:
    ObservedFileReaderFactory(std::string path, StorageObserver* observer, FileIoTiming* timing = nullptr,
                              FileReadOptions options = {}, uint8_t drive = 0)
        : path_(std::move(path)), observer_(observer), timing_(diagnosticTiming(timing)), options_(options), drive_(drive) {
        options_.validate();
        SdAccessScope access(observer_);
#ifdef ARDUINO_ARCH_ESP32
        FILINFO status{};
        FRESULT stat_result;
        {
            FileIoTime elapsed(timing_, &FileIoTiming::stat_us);
            if (timing_) ++timing_->stats;
            stat_result = f_stat(fatFsPath(drive_, path_).c_str(), &status);
        }
        if (stat_result != FR_OK || (status.fattrib & AM_DIR)) throw std::runtime_error("cannot stat: " + path_);
        size_ = status.fsize;
#else
        struct stat status{};
        int stat_result;
        {
            FileIoTime elapsed(timing_, &FileIoTiming::stat_us);
            if (timing_) ++timing_->stats;
            stat_result = stat(path_.c_str(), &status);
        }
        if (stat_result || status.st_size < 0) throw std::runtime_error("cannot stat: " + path_);
        size_ = static_cast<size_t>(status.st_size);
#endif
    }
    // Storage task only. Existing Readers retain their creation-time options.
    void setReadOptions(FileReadOptions options) {
        options.validate();
        options_ = options;
    }
    std::unique_ptr<galuchat::ByteReader> create(size_t offset = 0) const override {
        return std::make_unique<ObservedFileReader>(path_, size_, offset, observer_, timing_, options_, drive_);
    }
    std::unique_ptr<galuchat::ByteReader> createRange(size_t offset, size_t length) const override {
        if (offset > size_ || length > size_ - offset) throw std::runtime_error("invalid reader range");
        if (!length) return std::make_unique<galuchat::BufferReader>(nullptr, 0);
        auto options = options_;
        options.buffer_bytes = std::min(options.buffer_bytes, length);
        return std::make_unique<ObservedFileReader>(path_, offset + length, offset, observer_, timing_, options, drive_);
    }
private:
    std::string path_;
    StorageObserver* observer_;
    FileIoTiming* timing_;
    size_t size_ = 0;
    FileReadOptions options_;
    uint8_t drive_;
};
}  // namespace geoputer

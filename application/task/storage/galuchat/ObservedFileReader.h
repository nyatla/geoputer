#pragma once
#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <galuchat/wgsmap3_reader.hpp>
#include "../StorageActivity.h"
#include "ReadTiming.h"
#include "../../../Config.h"

namespace geoputer {
// SDK-compatible ReaderFactory. Observe actual buffered file operations, not
// each in-RAM readByte.
class ObservedFileReader final : public galuchat::ByteReader {
public:
    ObservedFileReader(const std::string& path, size_t source_size, size_t offset, StorageObserver* observer,
                       FileIoTiming* timing = nullptr)
        : observer_(observer), timing_(timing), buffer_(config::FileReadBufferBytes) {
        if (offset > source_size) throw std::runtime_error("invalid reader offset");
        SdAccessScope access(observer_);
        {
            FileIoTime elapsed(timing_, &FileIoTiming::open_us);
            if (timing_) ++timing_->opens;
            file_ = std::fopen(path.c_str(), "rb");
        }
        if (!file_) throw std::runtime_error("cannot open: " + path);
        int seek_result;
        {
            FileIoTime elapsed(timing_, &FileIoTiming::seek_us);
            if (timing_) ++timing_->seeks;
            seek_result = std::fseek(file_, static_cast<long>(offset), SEEK_SET);
        }
        if (seek_result) {
            FileIoTime elapsed(timing_, &FileIoTiming::close_us);
            if (timing_) ++timing_->closes;
            std::fclose(file_); file_ = nullptr;
            throw std::runtime_error("cannot seek: " + path);
        }
        length_ = source_size - offset;
    }
    ~ObservedFileReader() override {
        if (file_) {
            SdAccessScope access(observer_);
            FileIoTime elapsed(timing_, &FileIoTiming::close_us);
            if (timing_) ++timing_->closes;
            std::fclose(file_);
        }
    }
    size_t pos() const override { return pos_; }
    bool atEnd() const override { return pos_ == length_; }
protected:
    uint8_t nextByte() override {
        if (pos_ >= length_) throw std::runtime_error("unexpected end of file");
        if (buffer_pos_ == buffer_end_) {
            SdAccessScope access(observer_);
            {
                FileIoTime elapsed(timing_, &FileIoTiming::read_us);
                if (timing_) ++timing_->reads;
                buffer_end_ = std::fread(buffer_.data(), 1, std::min(buffer_.size(), length_ - pos_), file_);
                if (timing_) timing_->bytes += buffer_end_;
            }
            buffer_pos_ = 0;
            if (!buffer_end_) throw std::runtime_error("SD read failed");
        }
        ++pos_;
        return buffer_[buffer_pos_++];
    }
    void skipByte(size_t length) override {
        if (length > length_ - pos_) throw std::runtime_error("unexpected end of file");
        const auto buffered = buffer_end_ - buffer_pos_;
        if (length <= buffered) { buffer_pos_ += length; pos_ += length; return; }
        const auto seek = length - buffered;
        SdAccessScope access(observer_);
        int seek_result;
        {
            FileIoTime elapsed(timing_, &FileIoTiming::seek_us);
            if (timing_) ++timing_->seeks;
            seek_result = std::fseek(file_, static_cast<long>(seek), SEEK_CUR);
        }
        if (seek_result) throw std::runtime_error("SD seek failed");
        pos_ += length; buffer_pos_ = buffer_end_ = 0;
    }
private:
    StorageObserver* observer_;
    FileIoTiming* timing_;
    std::FILE* file_ = nullptr;
    std::vector<uint8_t> buffer_;
    size_t length_ = 0, pos_ = 0, buffer_pos_ = 0, buffer_end_ = 0;
};
class ObservedFileReaderFactory final : public galuchat::ReaderFactory {
public:
    ObservedFileReaderFactory(std::string path, StorageObserver* observer, FileIoTiming* timing = nullptr)
        : path_(std::move(path)), observer_(observer), timing_(timing) {
        SdAccessScope access(observer_);
        struct stat status{};
        int stat_result;
        {
            FileIoTime elapsed(timing_, &FileIoTiming::stat_us);
            if (timing_) ++timing_->stats;
            stat_result = stat(path_.c_str(), &status);
        }
        if (stat_result || status.st_size < 0) throw std::runtime_error("cannot stat: " + path_);
        size_ = static_cast<size_t>(status.st_size);
    }
    std::unique_ptr<galuchat::ByteReader> create(size_t offset = 0) const override {
        return std::make_unique<ObservedFileReader>(path_, size_, offset, observer_, timing_);
    }
private:
    std::string path_;
    StorageObserver* observer_;
    FileIoTiming* timing_;
    size_t size_ = 0;
};
}  // namespace geoputer

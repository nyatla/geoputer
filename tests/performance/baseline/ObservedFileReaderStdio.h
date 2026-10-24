#pragma once
// Unmodified stdio I/O path before FatFs direct access (2026-10-24).
// Class names and the factory's ignored drive parameter only support A/B tests.
#include "task/storage/galuchat/ObservedFileReader.h"
#include <cstdio>
#include <sys/stat.h>
namespace geoputer {
class StdioObservedFileReader final : public galuchat::ByteReader {
public:
    StdioObservedFileReader(const std::string& path, size_t source_size, size_t offset, StorageObserver* observer,
                       FileIoTiming* timing = nullptr, FileReadOptions options = {})
        : observer_(observer), timing_(timing), read_through_skip_bytes_(options.read_through_skip_bytes) {
        options.validate();
        if (offset > source_size) throw std::runtime_error("invalid reader offset");
        buffer_.resize(options.buffer_bytes);
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
    ~StdioObservedFileReader() override {
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
    void refillBuffer() {
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
    StorageObserver* observer_;
    FileIoTiming* timing_;
    std::FILE* file_ = nullptr;
    std::vector<uint8_t> buffer_;
    size_t length_ = 0, pos_ = 0, buffer_pos_ = 0, buffer_end_ = 0;
    const size_t read_through_skip_bytes_;
};
class StdioObservedFileReaderFactory final : public galuchat::ReaderFactory {
public:
    StdioObservedFileReaderFactory(std::string path, StorageObserver* observer, FileIoTiming* timing = nullptr,
                              FileReadOptions options = {}, uint8_t /*drive*/ = 0)
        : path_(std::move(path)), observer_(observer), timing_(timing), options_(options) {
        options_.validate();
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
    // Storage task only. Existing Readers retain their creation-time options.
    void setReadOptions(FileReadOptions options) {
        options.validate();
        options_ = options;
    }
    std::unique_ptr<galuchat::ByteReader> create(size_t offset = 0) const override {
        return std::make_unique<StdioObservedFileReader>(path_, size_, offset, observer_, timing_, options_);
    }
    std::unique_ptr<galuchat::ByteReader> createRange(size_t offset, size_t length) const override {
        if (offset > size_ || length > size_ - offset) throw std::runtime_error("invalid reader range");
        if (!length) return std::make_unique<galuchat::BufferReader>(nullptr, 0);
        auto options = options_;
        options.buffer_bytes = std::min(options.buffer_bytes, length);
        // The existing Reader treats source_size as its absolute upper bound.
        return std::make_unique<StdioObservedFileReader>(path_, offset + length, offset, observer_, timing_, options);
    }
private:
    std::string path_;
    StorageObserver* observer_;
    FileIoTiming* timing_;
    size_t size_ = 0;
    FileReadOptions options_;
};
}  // namespace geoputer

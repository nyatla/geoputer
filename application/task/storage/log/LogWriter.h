#pragma once
#include <cstdio>
#include <string>
#include "LogFormat.h"
#include "LogFile.h"
#include "../StorageActivity.h"
#include "../../../utils/ShortLock.h"

namespace geoputer {
// App produces records through append(); only Storage owns files and consumes them.
class LogWriter {
public:
    explicit LogWriter(std::string directory = "/sd/logs", StorageObserver* observer = nullptr)
        : directory_(std::move(directory)), observer_(observer) {}
    ~LogWriter() { if (file_.opened()) { SdAccessScope scope(observer_); file_.close(); } }
    LogWriter(const LogWriter&) = delete;
    LogWriter& operator=(const LogWriter&) = delete;
    void setDrive(uint8_t drive) { file_.setDrive(drive); }
    LogError open(uint64_t session, uint64_t created);
    LogError append(const LogCommand& command, uint32_t now_ms = 0);
    LogError requestFlush(uint64_t session, uint32_t boundary, bool close);
    bool flush(uint32_t now);
    bool needsFlush() const;
    bool hasRetainedSession() const;
    LogSnapshot snapshot() const;
    // Storage-only reader access uses this same FIL, never a second writer/reader handle.
    bool readSaved(uint64_t offset,uint8_t* bytes,size_t count,uint64_t session) {
        const auto state=snapshot();
        const uint64_t end=LogHeaderBytes+static_cast<uint64_t>(state.saved)*LogRecordBytes;
        return session==state.session_id && header_saved_ && file_.opened() && offset<=end && count<=end-offset &&
               file_.seek(offset) && file_.read(bytes,count);
    }
    // Active/retrying session only. snapshot().name remains after a successful close.
    const std::string& path() const { return path_; }
    uint32_t retries = 0;
private:
    bool createFile(uint64_t session, uint64_t first_utc);
    bool sameSessionExists(uint64_t session, bool& io_error);
    void changed() { if (!++state_.revision) ++state_.revision; }  // Caller holds lock_.
    mutable ShortLock lock_;
    uint8_t records_[LogBufferCount][LogRecordBytes]{};
    uint32_t sample_seqs_[LogBufferCount]{};
    uint8_t head_ = 0, batch_count_ = 0;
    uint32_t flush_sequence_ = 0, batch_flush_sequence_ = 0, after_batch_since_ms_ = 0;
    std::string directory_, path_;
    StorageObserver* observer_;
    LogFile file_;
    LogSnapshot state_{};
    uint64_t created_ = UINT64_MAX, first_utc_ = UINT64_MAX;
    bool flush_requested_ = false, header_saved_ = false;
};
}  // namespace geoputer

#pragma once
#include <dirent.h>
#include "LogReadTypes.h"
#include "LogWriter.h"

namespace geoputer {
// Storage-only, bounded directory scan. Results are fixed-size value copies.
class LogReader {
public:
    explicit LogReader(std::string directory="/sd/logs",StorageObserver* observer=nullptr)
        : directory_(std::move(directory)),observer_(observer) {}
    ~LogReader() { cancel(); }
    void setDrive(uint8_t drive) { drive_=drive; file_.setDrive(drive); }
    bool begin(const LogReadRequest& request);
    bool step(LogWriter* writer=nullptr); // At most 10 directory entries, or one <=10-record read.
    void cancel();
    void fail(LogReadError error);
    const LogReadReply& result() const { return result_; }
    bool done() const { return done_; }
private:
    void finishList(LogWriter* writer);
    void readTarget(LogWriter* writer);
    std::string directory_;
    StorageObserver* observer_;
    DIR* directory_handle_=nullptr;
    LogReadReply result_{};
    LogFile file_; // FatFs FIL includes a sector buffer; keep it off the task stack.
    uint32_t eligible_=0;
    uint8_t drive_=0xff;
    bool done_=true, started_=false;
};
bool validLogName(const char* name);
} // namespace geoputer

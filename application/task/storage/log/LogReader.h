#pragma once
#include <dirent.h>
#include "LogReadTypes.h"
#include "LogWriter.h"

namespace geoputer {
// Storage-only log operations and bounded scans. Results are fixed-size values.
class LogReader {
public:
    explicit LogReader(std::string directory="/sd/logs",StorageObserver* observer=nullptr)
        : directory_(std::move(directory)),observer_(observer) {}
    ~LogReader() { cancel(); }
    void setDrive(uint8_t drive) { drive_=drive; file_.setDrive(drive); }
    bool begin(const LogReadRequest& request);
    bool step(LogWriter* writer=nullptr); // At most 10 directory entries, one <=10-record read, or 8 statistics records.
    void cancel();
    bool releaseForReuse(); // Release path capacity only after terminal read and successful close.
    void fail(LogReadError error);
    const LogReadReply& result() const { return result_; }
    bool done() const { return done_; }
    LogStatisticsProgress progress() const;
private:
    void finishList(LogWriter* writer);
    const std::string& pathFor(const char* name);
    void readTarget(LogWriter* writer);
    void deleteTarget(LogWriter* writer);
    void stepStatistics(LogWriter* writer);
    LogStatisticsAccumulator statistics_{};
    uint64_t statistics_started_=0;
    std::string directory_, path_; // Reuse path capacity while this Reader remains active.
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

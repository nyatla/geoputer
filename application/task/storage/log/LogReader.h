#pragma once
#include <dirent.h>
#include "LogReadTypes.h"
#include "LogWriter.h"
#include "../ReadFile.h"

namespace geoputer {
// Storage-only log operations and bounded scans. Results are fixed-size values.
class LogReader {
public:
    explicit LogReader(std::string_view directory=config::LogDirectory,StorageObserver* observer=nullptr,ReadFileSlot* file_slot=nullptr)
        : directory_(directory),observer_(observer),file_(0,file_slot) {}
    ~LogReader() { cancel(); }
    void setDrive(uint8_t drive) { drive_=drive; file_.setDrive(drive); }
    bool begin(const LogReadRequest& request);
    bool step(LogWriter* writer=nullptr); // At most 10 directory entries, one <=10-record read, or 8 statistics records.
    void cancel();
    bool releaseForReuse(); // Reuse only after terminal read and successful close.
    void fail(LogReadError error);
    const LogReadReply& result() const { return result_; }
    bool done() const { return done_; }
    LogStatisticsProgress progress() const;
private:
    void finishList(LogWriter* writer);
    [[gnu::noinline]] LogReadError readHeader(LogWriter* writer,uint64_t session,LogSummary& summary);
    void readTarget(LogWriter* writer);
    void deleteTarget(LogWriter* writer);
    void stepStatistics(LogWriter* writer);
    LogStatisticsAccumulator statistics_{};
    uint64_t statistics_started_=0;
    LogDirectory directory_; // Borrowed root; target name stays in result_.request.
    StorageObserver* observer_;
    DIR* directory_handle_=nullptr;
    LogReadReply result_{};
    ReadFile file_; // StorageWorkArea supplies the stable primary FIL.
    uint32_t eligible_=0;
    uint8_t drive_=0xff;
    bool done_=true, started_=false;
};
bool validLogName(const char* name);
} // namespace geoputer

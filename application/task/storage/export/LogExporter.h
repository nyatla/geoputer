#pragma once
#include "ExportTypes.h"
#include "../log/LogFile.h"
#include "../../../Config.h"

namespace geoputer {
// Storage-owned streaming converter. No full-log buffer and no task/queue.
class LogExporter {
public:
    explicit LogExporter(std::string root=config::DataRoot, uint64_t byte_limit=UINT32_MAX)
        : root_(std::move(root)), byte_limit_(byte_limit) {}
    void setDrive(uint8_t drive) { drive_=drive; input_.setDrive(drive); output_.setDrive(drive); }
    bool begin(const char* name, ExportFormat format);
    void step();
    bool closeFiles();
    const ExportSnapshot& snapshot() const { return state_; }
    bool running() const { return state_.phase==ExportPhase::Running; }
private:
    bool open();
    bool append(const char* text);
    bool flush();
    bool record(const uint8_t* bytes);
    bool finish();
    bool moveComplete();
    void fail(ExportError error);
    std::string root_, target_, partial_;
    LogFile input_, output_;
    ExportSnapshot state_{};
    uint64_t byte_limit_, written_=0;
    size_t buffered_=0;
    uint8_t drive_=0;
    bool started_=false, owns_partial_=false;
    uint8_t read_buffer_[512]{};
    char output_buffer_[config::ExportWriteBufferBytes]{};
    char line_[384]{};
};
}

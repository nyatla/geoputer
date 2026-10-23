#pragma once
#include "../../../utils/ElapsedTime.h"

namespace geoputer {
struct FileIoTiming {
    uint64_t elapsed_us = 0, bytes = 0;
    uint64_t read_us = 0, seek_us = 0, open_us = 0, close_us = 0, stat_us = 0;
    uint32_t reads = 0, seeks = 0, opens = 0, closes = 0, stats = 0;
};
class FileIoTime {
public:
    FileIoTime(FileIoTiming* timing, uint64_t FileIoTiming::* category)
        : timing_(timing), category_(category), start_(timing ? monotonicMicros() : 0) {}
    ~FileIoTime() {
        if (timing_) {
            const auto elapsed = monotonicMicros() - start_;
            timing_->elapsed_us += elapsed;
            timing_->*category_ += elapsed;
        }
    }
    FileIoTime(const FileIoTime&) = delete;
    FileIoTime& operator=(const FileIoTime&) = delete;
private:
    FileIoTiming* timing_;
    uint64_t FileIoTiming::* category_;
    uint64_t start_;
};
struct PlaceReadTiming {
    uint64_t total_us = 0, open_us = 0, map_us = 0, rect_us = 0, wordbook_us = 0, text_us = 0;
    FileIoTiming map_io{}, wordbook_io{}, index_io{};
};
} // namespace geoputer

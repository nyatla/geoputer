#pragma once
// Diagnostic firmware only: subtract nested file I/O from decoder wall time.
#include "task/storage/galuchat/ReadTiming.h"
namespace geoputer::benchmark {
struct DecodeProfile {
    FileIoTiming* map_io = nullptr;
    FileIoTiming* index_io = nullptr;
    uint64_t inclusive_us = 0, io_us = 0;
    unsigned calls = 0;
    uint64_t io() const { return map_io->elapsed_us + index_io->elapsed_us; }
};
inline DecodeProfile* active_decode_profile = nullptr;
class DecodeScope {
public:
    DecodeScope() : profile_(active_decode_profile) {
        if (profile_) { io_ = profile_->io(); start_ = monotonicMicros(); }
    }
    ~DecodeScope() {
        if (profile_) {
            const auto elapsed = monotonicMicros() - start_;
            const auto io = profile_->io() - io_;
            profile_->inclusive_us += elapsed;
            profile_->io_us += io;
            ++profile_->calls;
        }
    }
private:
    DecodeProfile* profile_;
    uint64_t start_ = 0, io_ = 0;
};
class ActivateDecodeProfile {
public:
    explicit ActivateDecodeProfile(DecodeProfile& profile) { active_decode_profile = &profile; }
    ~ActivateDecodeProfile() { active_decode_profile = nullptr; }
};
} // namespace geoputer::benchmark

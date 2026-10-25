#pragma once
#include <cstdint>
#include "../../gps/GpsTypes.h"

namespace geoputer {
enum class RecordingState : uint8_t { Stopped, Starting, Recording, Paused };
enum class LogError : uint8_t { None, Busy, Invalid, BufferFull, Io, SessionExists, Capacity };
struct LogSnapshot {
    uint64_t session_id = 0;
    uint32_t revision = 0;
    uint32_t accepted = 0, saved = 0, accepted_seq = 0, saved_seq = 0;
    uint32_t last_saved_ms = 0;
    uint32_t buffer_since_ms = 0;
    uint8_t buffered = 0;
    bool open = false, closing = false;
    LogError error = LogError::None;
    char name[48]{}; // Assigned by Storage when the first physical file is created.
};
inline bool applyLogSnapshot(LogSnapshot& current, const LogSnapshot& incoming) {
    if (!incoming.revision || (current.revision && static_cast<int32_t>(incoming.revision-current.revision)<=0)) return false;
    current = incoming;
    return true;
}
struct LogCommand {
    uint64_t session_id;
    uint64_t created_utc_ms;
    uint32_t sample_seq;
    GpsFix fix;
};
struct RecordingSnapshot {
    RecordingState state = RecordingState::Stopped;
    uint32_t elapsed_ms = 0, dropped = 0, now_ms = 0;
    bool pending = false;
};
// A paused or retained session still owns log state and blocks mode/dataset changes.
inline bool loggerStopped(const RecordingSnapshot& recording, const LogSnapshot& log) {
    return recording.state == RecordingState::Stopped && !recording.pending &&
           !log.open && !log.closing && !log.buffered;
}
inline const char* logErrorName(LogError error) {
    switch (error) {
        case LogError::None: return "";
        case LogError::BufferFull: return "SD BUFFER FULL";
        case LogError::Io: return "SD WRITE FAILED";
        case LogError::Capacity: return "SD FILE LIMIT";
        case LogError::SessionExists: return "LOG SESSION EXISTS";
        case LogError::Busy: return "LOG SESSION BUSY";
        case LogError::Invalid: return "INVALID LOG REQUEST";
    }
    return "LOG ERROR";
}
}  // namespace geoputer

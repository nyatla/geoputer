#pragma once
#include <cstddef>
#include <cstdint>
#include "../../gps/GpsTypes.h"

namespace geoputer {
constexpr unsigned LogListRows=6, LogReadCount=10, LogNameBytes=48;
enum class LogReadKind : uint8_t { List, Summary, Records, Delete };
enum class LogListDirection : uint8_t { Newest, Older, Newer, FromAnchor };
enum class LogReadError : uint8_t { None, Io, InvalidName, Header, Unsupported, Corrupt, Range, Changed, Cancelled, NoCard, SaveFailed, Active, Unconfirmed, DeleteFailed, NotFound };
struct LogSummary {
    uint64_t session_id, bytes, first_utc, last_utc;
    uint32_t records;
    uint8_t tail;
};
struct LogReadRequest {
    uint64_t session_id;
    uint32_t token, screen_generation, start, limit;
    LogReadKind kind;
    LogListDirection direction;
    uint8_t count;
    bool confirmed;
    char name[LogNameBytes], anchor[LogNameBytes];
};
struct LogReadReply {
    LogReadRequest request;
    LogSummary summary;
    uint32_t failed_record;
    LogReadError error;
    uint8_t count, active_mask;
    bool has_newer, has_older, eof;
    union { uint8_t records[LogReadCount][32]; char names[LogListRows][LogNameBytes]; } data;
};
inline const char* logReadErrorName(LogReadError error) {
    switch(error) {
        case LogReadError::None:return "";
        case LogReadError::Io:return "SD READ FAILED";
        case LogReadError::InvalidName:return "INVALID LOG NAME";
        case LogReadError::Header:return "INVALID LOG HEADER";
        case LogReadError::Unsupported:return "UNSUPPORTED LOG FORMAT";
        case LogReadError::Corrupt:return "RECORD CRC / DATA ERROR";
        case LogReadError::Range:return "INVALID RECORD RANGE";
        case LogReadError::Changed:return "FILE CHANGED; RELOAD";
        case LogReadError::Cancelled:return "CANCELLED";
        case LogReadError::NoCard:return "SD NOT AVAILABLE";
        case LogReadError::SaveFailed:return "SAVE FAILED; READ CANCELLED";
        case LogReadError::Active:return "LOG IS IN USE";
        case LogReadError::Unconfirmed:return "DELETE NOT CONFIRMED";
        case LogReadError::DeleteFailed:return "SD DELETE FAILED";
        case LogReadError::NotFound:return "LOG FILE NOT FOUND";
    }
    return "LOG READ ERROR";
}
struct LogBrowserView {
    LogSummary summary;
    GpsFix point;
    uint32_t revision, index, target, page;
    char names[LogListRows][LogNameBytes], name[LogNameBytes];
    LogReadError error;
    uint8_t count, selected, active_mask;
    bool loading, has_newer, has_older, deleting;
    bool point_loaded=false, map_mode=false, playing=false;
};
} // namespace geoputer

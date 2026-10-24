#pragma once
#include <cstdint>
#include "../log/LogReadTypes.h"

namespace geoputer {
enum class ExportFormat : uint8_t { Gpx, Kml, Csv };
enum class ExportPhase : uint8_t { Idle, WaitEject, Preparing, Selecting, Running, Publishing, Complete, Failed };
enum class ExportOperation : uint8_t { Begin, Start, Publish };
enum class ExportError : uint8_t { None, Media, InvalidName, Header, Unsupported, Corrupt, Tail, Empty,
    Exists, PartialExists, Read, Write, Directory, Limit, Time, Publish, Close, Memory };
constexpr unsigned ExportNameBytes = LogNameBytes + 4;
struct ExportRequest {
    ExportOperation operation;
    ExportFormat format;
    char name[LogNameBytes];
};
struct ExportSnapshot {
    ExportPhase phase = ExportPhase::Idle;
    ExportFormat format = ExportFormat::Gpx;
    ExportError error = ExportError::None;
    bool output_created = false;
    uint32_t processed = 0, total = 0, failed_record = 0;
    char name[LogNameBytes]{};
};
inline bool validExportFormat(ExportFormat f) { return f == ExportFormat::Gpx || f == ExportFormat::Kml || f == ExportFormat::Csv; }
inline const char* exportExtension(ExportFormat f) {
    switch(f) { case ExportFormat::Gpx:return ".gpx";case ExportFormat::Kml:return ".kml";case ExportFormat::Csv:return ".csv"; }
    return "";
}
inline const char* exportFormatName(ExportFormat f) {
    switch(f) { case ExportFormat::Gpx:return "GPX";case ExportFormat::Kml:return "KML";case ExportFormat::Csv:return "CSV"; }
    return "?";
}
inline const char* exportErrorName(ExportError e) {
    switch(e) {
        case ExportError::None:return "";
        case ExportError::Media:return "SD NOT AVAILABLE";
        case ExportError::InvalidName:return "INVALID LOG NAME";
        case ExportError::Header:return "INVALID LOG HEADER";
        case ExportError::Unsupported:return "UNSUPPORTED LOG FORMAT";
        case ExportError::Corrupt:return "RECORD CRC / DATA ERROR";
        case ExportError::Tail:return "INCOMPLETE LOG TAIL";
        case ExportError::Empty:return "LOG HAS NO POINTS";
        case ExportError::Exists:return "OUTPUT ALREADY EXISTS";
        case ExportError::PartialExists:return "REMOVE STALE .PART ON PC";
        case ExportError::Read:return "SD READ FAILED";
        case ExportError::Write:return "SD WRITE / SYNC FAILED";
        case ExportError::Directory:return "EXPORT DIRECTORY ERROR";
        case ExportError::Limit:return "EXPORT EXCEEDS FAT32 LIMIT";
        case ExportError::Time:return "LOG TIME OUT OF RANGE";
        case ExportError::Publish:return "USB REPUBLISH FAILED";
        case ExportError::Close:return "FILE CLOSE FAILED";
        case ExportError::Memory:return "NOT ENOUGH MEMORY";
    }
    return "EXPORT ERROR";
}
}

#pragma once

#include <cstdint>

namespace geoputer {

// Reception state only. These values do not claim GNSS power/control state.
enum class GpsReception : uint8_t { Waiting, NoData, NoFix, Fix, Unavailable };

enum GpsField : uint8_t { GpsTime = 2, GpsAltitude = 4, GpsSpeed = 8, GpsCourse = 16, GpsHdop = 32, GpsSatellites = 64 };

struct GpsFix {
    double latitude;
    double longitude;
    uint32_t utc_ms;  // Milliseconds since UTC midnight; not the local clock.
    bool valid;
    uint64_t utc_epoch_ms = UINT64_MAX;
    float altitude_m = 0;
    float speed_kmh = 0;
    float course_deg = 0;
    float hdop = 0;
    uint8_t satellites = 0;
    uint8_t fields = 0;
};

inline void mergeGpsFix(GpsFix& target, const GpsFix& incoming) {
    if (incoming.valid) { target.latitude = incoming.latitude; target.longitude = incoming.longitude; target.valid = true; }
    if (incoming.fields & GpsTime) target.utc_epoch_ms = incoming.utc_epoch_ms;
    if (incoming.fields & GpsAltitude) target.altitude_m = incoming.altitude_m;
    if (incoming.fields & GpsSpeed) target.speed_kmh = incoming.speed_kmh;
    if (incoming.fields & GpsCourse) target.course_deg = incoming.course_deg;
    if (incoming.fields & GpsHdop) target.hdop = incoming.hdop;
    if (incoming.fields & GpsSatellites) target.satellites = incoming.satellites;
    target.fields |= incoming.fields;
}

struct GpsSnapshot {
    uint32_t revision;
    uint32_t sample_seq;
    GpsReception reception;
    GpsFix fix;
    uint32_t received_at_ms = 0;
    bool received = false;
};

inline bool applyGpsSnapshot(GpsSnapshot& current, const GpsSnapshot& incoming) {
    if (incoming.revision == 0 ||
        (current.revision != 0 && static_cast<int32_t>(incoming.revision - current.revision) <= 0)) return false;
    current = incoming;
    return true;
}

inline const char* gpsReceptionName(GpsReception state) {
    switch (state) {
        case GpsReception::Waiting: return "WAITING";
        case GpsReception::NoData: return "NO GPS";
        case GpsReception::NoFix: return "NO FIX";
        case GpsReception::Fix: return "FIX";
        case GpsReception::Unavailable: return "ERROR";
    }
    return "ERROR";
}

}  // namespace geoputer

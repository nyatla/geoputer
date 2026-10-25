#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../gps/GpsTypes.h"

namespace geoputer {
struct GpsText {
    char speed[16], heading[8], latitude[16], longitude[16], altitude[24], satellites[8], hdop[16];
};

// Format optional fields independently: missing data must never look like zero.
inline GpsText formatGpsText(const GpsSnapshot& gps, const GpsFix& place_fix = {}, bool details = false) {
    GpsText text{};
    std::snprintf(text.speed, sizeof(text.speed), "--");
    std::snprintf(text.heading, sizeof(text.heading), "--");
    std::snprintf(text.latitude, sizeof(text.latitude), "--");
    std::snprintf(text.longitude, sizeof(text.longitude), "--");
    std::snprintf(text.altitude, sizeof(text.altitude), "--");
    std::snprintf(text.satellites, sizeof(text.satellites), "--");
    std::snprintf(text.hdop, sizeof(text.hdop), "--");
    const auto& fix = gps.fix;
    const bool fixed = fix.valid && gps.reception == GpsReception::Fix;
    // The GPS detail screen retains received quality fields while waiting for
    // a fix. LIVE clears them with the positional fields, as before.
    if (fixed || details) {
        if (fix.fields & GpsSatellites)
            std::snprintf(text.satellites,sizeof(text.satellites),"%u",fix.satellites);
        if (details && (fix.fields & GpsHdop) && std::isfinite(fix.hdop))
            std::snprintf(text.hdop,sizeof(text.hdop),"%.1f",fix.hdop);
    }
    if (!fixed) return text;
    const auto& coordinate = place_fix.valid ? place_fix : fix;
    if (std::isfinite(coordinate.latitude) && std::abs(coordinate.latitude) <= 90)
        std::snprintf(text.latitude, sizeof(text.latitude), "%+.6f", coordinate.latitude);
    if (std::isfinite(coordinate.longitude) && std::abs(coordinate.longitude) <= 180)
        std::snprintf(text.longitude, sizeof(text.longitude), "%+.6f", coordinate.longitude);
    if ((fix.fields & GpsSpeed) && std::isfinite(fix.speed_kmh) && fix.speed_kmh >= 0) {
        // Keep km/h and at most three digits. Discard sub-1 km/h movement;
        // promote rounded boundaries before selecting the decimal precision.
        const double kmh = fix.speed_kmh;
        const double hundredths = std::round(kmh * 100) / 100;
        const double tenths = std::round(kmh * 10) / 10;
        if (kmh < 1) {
            std::snprintf(text.speed, sizeof(text.speed), "0");
        } else if (hundredths < 10) {
            std::snprintf(text.speed, sizeof(text.speed), "%.2f", hundredths);
        } else if (tenths < 100) {
            std::snprintf(text.speed, sizeof(text.speed), "%.1f", tenths);
        } else if (std::round(kmh) <= 999) {
            std::snprintf(text.speed, sizeof(text.speed), "%.0f", std::round(kmh));
        } else std::snprintf(text.speed, sizeof(text.speed), "OVR");
    }
    if ((fix.fields & GpsCourse) && std::isfinite(fix.course_deg) && fix.course_deg >= 0 && fix.course_deg < 360)
        std::snprintf(text.heading, sizeof(text.heading), "%ld", std::lround(fix.course_deg) % 360);
    if ((fix.fields & GpsAltitude) && std::isfinite(fix.altitude_m)) {
        const auto count = std::snprintf(text.altitude, sizeof(text.altitude), "%.1f", fix.altitude_m);
        if (count < 0 || static_cast<size_t>(count) >= sizeof(text.altitude)) std::snprintf(text.altitude, sizeof(text.altitude), "OVR");
    }
    return text;
}

// LIVE has an eleven-cell altitude field including its unit. GPS details draw
// the same number and a separately anchored unit, without reformatting a float.
inline const char* liveAltitude(const GpsText& text, char (&out)[16]) {
    if (!std::strcmp(text.altitude,"--") || !std::strcmp(text.altitude,"OVR")) return text.altitude;
    if (std::strlen(text.altitude)+1>11) return "OVR";
    std::snprintf(out,sizeof(out),"%.10sm",text.altitude);
    return out;
}
} // namespace geoputer

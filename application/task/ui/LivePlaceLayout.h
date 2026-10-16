#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>
#include "../gps/GpsTypes.h"

namespace geoputer {
namespace LivePlaceLayout {
constexpr int AddressX = 4, AddressY = 20, AddressStep = 20;
constexpr int AddressBottom = 60, DividerX = 120, SpeedBottom = 83;
constexpr int InfoBottom = 106, FooterTop = 120;
constexpr int DetailLabelX = 122, DetailValueX = 148, DetailY = 65, DetailStep = 10;
constexpr int LogY = 110, ErrorY = 107, ErrorHeight = 13;
constexpr int SpeedRight = 84, SpeedY = 66, SpeedUnitX = 84, SpeedUnitY = 73;
}

inline int speedTextWidth(const char* text) {
    int width = 0;
    for (; *text; ++text) width += *text == '.' ? 8 : 16;
    return width;
}

struct LiveGpsText {
    char speed[16], speed_unit[5], heading[16], latitude[16], longitude[16], altitude[16], satellites[8];
};

// Format optional fields independently: missing data must never look like zero.
inline LiveGpsText formatLiveGps(const GpsSnapshot& gps, const GpsFix& place_fix) {
    LiveGpsText text{};
    std::snprintf(text.speed, sizeof(text.speed), "--");
    std::snprintf(text.speed_unit, sizeof(text.speed_unit), "km/h");
    std::snprintf(text.heading, sizeof(text.heading), "--°");
    std::snprintf(text.latitude, sizeof(text.latitude), "--");
    std::snprintf(text.longitude, sizeof(text.longitude), "--");
    std::snprintf(text.altitude, sizeof(text.altitude), "--");
    std::snprintf(text.satellites, sizeof(text.satellites), "--");
    if (!gps.fix.valid || gps.reception != GpsReception::Fix) return text;
    const auto& fix = gps.fix;
    const auto& coordinate = place_fix.valid ? place_fix : fix;
    if (std::isfinite(coordinate.latitude) && std::abs(coordinate.latitude) <= 90)
        std::snprintf(text.latitude, sizeof(text.latitude), "%+.6f", coordinate.latitude);
    if (std::isfinite(coordinate.longitude) && std::abs(coordinate.longitude) <= 180)
        std::snprintf(text.longitude, sizeof(text.longitude), "%+.6f", coordinate.longitude);
    if ((fix.fields & GpsSpeed) && std::isfinite(fix.speed_kmh) && fix.speed_kmh >= 0) {
        // Promote a rounded boundary before formatting: never emit 1000m/h
        // or 100.0km/h in the three-digit speed field.
        const double kmh = fix.speed_kmh;
        const double metres = std::round(kmh * 1000);
        const double tenths = std::round(kmh * 10) / 10;
        if (metres < 1000) {
            std::snprintf(text.speed_unit, sizeof(text.speed_unit), "m/h");
            std::snprintf(text.speed, sizeof(text.speed), "%.0f", metres);
        } else if (tenths < 100) {
            std::snprintf(text.speed, sizeof(text.speed), "%.1f", tenths);
        } else if (std::round(kmh) <= 999) {
            std::snprintf(text.speed, sizeof(text.speed), "%.0f", std::round(kmh));
        } else std::snprintf(text.speed, sizeof(text.speed), "OVR");
    }
    if ((fix.fields & GpsCourse) && std::isfinite(fix.course_deg) && fix.course_deg >= 0 && fix.course_deg < 360)
        std::snprintf(text.heading, sizeof(text.heading), "%ld°", std::lround(fix.course_deg) % 360);
    if ((fix.fields & GpsAltitude) && std::isfinite(fix.altitude_m)) {
        const auto count = std::snprintf(text.altitude, sizeof(text.altitude), "%.1fm", fix.altitude_m);
        if (count < 0 || count > 11) std::snprintf(text.altitude, sizeof(text.altitude), "OVR");
    }
    if (fix.fields & GpsSatellites)
        std::snprintf(text.satellites, sizeof(text.satellites), "%u", fix.satellites);
    return text;
}
}  // namespace geoputer

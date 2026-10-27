#pragma once
#include <algorithm>
#include <cmath>
#include "../../gps/GpsTypes.h"
namespace geoputer {
struct LogStatistics {
    double distance_m=0;
    uint64_t read_us=0, decode_us=0, math_us=0;
    float max_speed_kmh=0, min_altitude_m=0, max_altitude_m=0;
    uint32_t processed=0, elapsed_ms=0, max_step_us=0;
    bool has_speed=false, has_altitude=false;
};
// Fixed-size streaming accumulator. No file, RTOS, UI or heap dependencies.
class LogStatisticsAccumulator {
public:
    LogStatistics value{};
    void add(const GpsFix& fix) {
        constexpr double rad=3.14159265358979323846/180.0;
        if(value.processed) {
            const double a=(fix.latitude-latitude_)*rad/2;
            const double b=(fix.longitude-longitude_)*rad/2;
            const double h=std::clamp(std::sin(a)*std::sin(a)+std::cos(latitude_*rad)*
                std::cos(fix.latitude*rad)*std::sin(b)*std::sin(b),0.0,1.0);
            value.distance_m+=6371008.8*2*std::atan2(std::sqrt(h),std::sqrt(1-h));
        }
        latitude_=fix.latitude;longitude_=fix.longitude;
        if(fix.fields&GpsSpeed) {
            value.max_speed_kmh=value.has_speed?std::max(value.max_speed_kmh,fix.speed_kmh):fix.speed_kmh;
            value.has_speed=true;
        }
        if(fix.fields&GpsAltitude) {
            value.min_altitude_m=value.has_altitude?std::min(value.min_altitude_m,fix.altitude_m):fix.altitude_m;
            value.max_altitude_m=value.has_altitude?std::max(value.max_altitude_m,fix.altitude_m):fix.altitude_m;
            value.has_altitude=true;
        }
        ++value.processed;
    }
private:
    double latitude_=0,longitude_=0;
};
struct LogStatisticsProgress {
    uint32_t token,screen_generation,processed,total,elapsed_ms;
};
} // namespace geoputer

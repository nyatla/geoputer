#pragma once
#include "../gps/GpsTime.h"

namespace geoputer {
inline void formatLogDateTime(uint64_t utc,char* out,size_t size) {
    char raw[24];
    if(!formatJstFilenameTime(utc,raw,sizeof(raw))) { std::snprintf(out,size,"--"); return; }
    std::snprintf(out,size,"%.4s-%.2s-%.2s %.2s:%.2s:%.2s",raw,raw+4,raw+6,raw+9,raw+11,raw+13);
}
} // namespace geoputer

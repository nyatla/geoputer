#pragma once
#include "../gps/GpsTime.h"
#include "../storage/log/LogReadTypes.h"
#include <algorithm>
#include <cstring>

namespace geoputer {
inline void logListDisplayName(const char* name,char (&out)[27]) {
    const auto length=strnlen(name,LogNameBytes);
    const auto stem=length>=8 && !std::memcmp(name+length-8,".gps.dat",8)?length-8:length;
    const auto count=std::min<size_t>(stem,26);
    std::memcpy(out,name,count);out[count]='\0';
    if(stem>26)std::memcpy(out+23,"...",4);
}
inline void formatLogDateTime(uint64_t utc,char* out,size_t size) {
    char raw[24];
    if(!formatJstFilenameTime(utc,raw,sizeof(raw))) { std::snprintf(out,size,"--"); return; }
    std::snprintf(out,size,"%.4s-%.2s-%.2s %.2s:%.2s:%.2s",raw,raw+4,raw+6,raw+9,raw+11,raw+13);
}
} // namespace geoputer

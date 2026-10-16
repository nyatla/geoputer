#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include "LogTypes.h"

namespace geoputer {
constexpr size_t LogHeaderBytes = 512, LogRecordBytes = 32, LogBufferCount = 64;
constexpr uint32_t LogMaxRecords = 134217711;
inline uint32_t logCrc32(const uint8_t* data, size_t count) {
    uint32_t crc = 0xffffffffu;
    while (count--) {
        crc ^= *data++;
        for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ (crc & 1 ? 0xedb88320u : 0);
    }
    return crc ^ 0xffffffffu;
}
inline void logPut(uint8_t* bytes, uint64_t value, unsigned length) {
    for (unsigned i = 0; i < length; ++i) bytes[i] = static_cast<uint8_t>(value >> (8*i));
}
inline uint64_t logGet(const uint8_t* bytes, unsigned length) {
    uint64_t value = 0;
    for (unsigned i = 0; i < length; ++i) value |= static_cast<uint64_t>(bytes[i]) << (8*i);
    return value;
}
inline void encodeLogHeader(uint8_t (&header)[512], uint64_t session, uint64_t created) {
    std::memset(header, 0, sizeof(header));
    std::memcpy(header, "GPTLOG\r\n", 8);
    logPut(header+8,1,2); logPut(header+10,512,2); logPut(header+12,32,2);
    logPut(header+16,session,8); logPut(header+24,created,8);
    logPut(header+508,logCrc32(header,508),4);
}
inline bool validLogHeader(const uint8_t* header) {
    if (std::memcmp(header,"GPTLOG\r\n",8) || logGet(header+8,2)!=1 || logGet(header+10,2)!=512 ||
        logGet(header+12,2)!=32 || !logGet(header+16,8) || logGet(header+14,2) ||
        logGet(header+508,4)!=logCrc32(header,508)) return false;
    for (unsigned i=32;i<508;++i) if (header[i]) return false;
    return true;
}
inline bool encodeLogRecord(const GpsFix& fix, uint8_t (&record)[32]) {
    if (!fix.valid || !std::isfinite(fix.latitude) || !std::isfinite(fix.longitude) ||
        std::abs(fix.latitude)>90 || std::abs(fix.longitude)>180) return false;
    std::memset(record,0,sizeof(record));
    logPut(record,fix.fields & GpsTime ? fix.utc_epoch_ms : UINT64_MAX,8);
    logPut(record+8,static_cast<uint32_t>(std::llround(fix.latitude*1e7)),4);
    logPut(record+12,static_cast<uint32_t>(std::llround(fix.longitude*1e7)),4);
    record[27]=1;
    if ((fix.fields & GpsTime) && fix.utc_epoch_ms!=UINT64_MAX) record[27]|=GpsTime;
    else logPut(record,UINT64_MAX,8);
    auto field = [&](unsigned flag, float value, double scale, double min, double max, unsigned offset, unsigned bytes) {
        if (!(fix.fields & flag) || !std::isfinite(value)) return;
        const double scaled=static_cast<double>(value)*scale;
        if (scaled<min-0.5 || scaled>max+0.499999) return;
        const int64_t rounded=std::llround(scaled);
        if (rounded<min || rounded>max) return;
        logPut(record+offset,static_cast<uint64_t>(rounded),bytes); record[27]|=flag;
    };
    field(GpsAltitude,fix.altitude_m,1000,INT32_MIN,INT32_MAX,16,4);
    field(GpsSpeed,fix.speed_kmh,100.0/3.6,0,65535,20,2);
    if ((fix.fields & GpsCourse) && std::isfinite(fix.course_deg) && fix.course_deg>=0 && fix.course_deg<360) {
        logPut(record+22,static_cast<uint16_t>(std::lround(fix.course_deg*100))%36000,2); record[27]|=GpsCourse;
    }
    field(GpsHdop,fix.hdop,100,0,65535,24,2);
    if (fix.fields & GpsSatellites) { record[26]=fix.satellites; record[27]|=GpsSatellites; }
    logPut(record+28,logCrc32(record,28),4);
    return true;
}
inline bool decodeLogRecord(const uint8_t* record, GpsFix& fix) {
    if (logGet(record+28,4)!=logCrc32(record,28) || !(record[27]&1) || (record[27]&128)) return false;
    const auto flags=record[27];
    const auto lat=static_cast<int32_t>(logGet(record+8,4)), lon=static_cast<int32_t>(logGet(record+12,4));
    if (lat < -900000000 || lat > 900000000 || lon < -1800000000 || lon > 1800000000) return false;
    const auto utc=logGet(record,8);
    if ((flags&GpsTime) ? utc==UINT64_MAX : utc!=UINT64_MAX) return false;
    for (const auto field : {GpsAltitude,GpsSpeed,GpsCourse,GpsHdop,GpsSatellites}) {
        const unsigned offset=field==GpsAltitude?16:field==GpsSpeed?20:field==GpsCourse?22:field==GpsHdop?24:26;
        const unsigned bytes=field==GpsAltitude?4:field==GpsSatellites?1:2;
        if (!(flags&field) && logGet(record+offset,bytes)) return false;
    }
    if ((flags&GpsCourse) && logGet(record+22,2)>=36000) return false;
    GpsFix value{};
    value.valid=true; value.latitude=lat/1e7; value.longitude=lon/1e7;
    value.fields=flags&126; value.utc_epoch_ms=utc;
    value.utc_ms=flags&GpsTime ? utc%86400000ull : UINT32_MAX;
    value.altitude_m=static_cast<int32_t>(logGet(record+16,4))/1000.0f;
    value.speed_kmh=logGet(record+20,2)*0.036f; value.course_deg=logGet(record+22,2)/100.0f;
    value.hdop=logGet(record+24,2)/100.0f; value.satellites=record[26];
    fix=value; return true;
}
}  // namespace geoputer

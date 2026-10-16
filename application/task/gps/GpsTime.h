#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace geoputer {
inline bool leapYear(unsigned year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
inline unsigned monthDays(unsigned year, unsigned month) {
    constexpr unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return month >= 1 && month <= 12 ? days[month - 1] + (month == 2 && leapYear(year)) : 0;
}
inline bool nmeaEpochMs(const char* date, uint32_t utc_ms, uint64_t& out) {
    if (std::strlen(date) != 6 || utc_ms >= 86400000) return false;
    for (unsigned i = 0; i < 6; ++i) if (date[i] < '0' || date[i] > '9') return false;
    const unsigned day = (date[0]-'0')*10 + date[1]-'0';
    const unsigned month = (date[2]-'0')*10 + date[3]-'0';
    const unsigned yy = (date[4]-'0')*10 + date[5]-'0';
    const unsigned year = yy >= 80 ? 1900 + yy : 2000 + yy;
    if (!day || day > monthDays(year, month)) return false;
    uint64_t days = day - 1;
    for (unsigned y = 1970; y < year; ++y) days += leapYear(y) ? 366 : 365;
    for (unsigned m = 1; m < month; ++m) days += monthDays(year, m);
    out = days * 86400000ull + utc_ms;
    return true;
}
inline bool formatJstFilenameTime(uint64_t utc_ms, char* out, size_t size) {
    if (utc_ms == UINT64_MAX || utc_ms / 86400000ull >= 2932897) return false;
    const uint64_t local = utc_ms / 1000 + 9 * 3600;
    uint64_t days = local / 86400;
    unsigned year = 1970, month = 1;
    while (days >= (leapYear(year) ? 366u : 365u)) { days -= leapYear(year) ? 366 : 365; ++year; }
    if (year > 9999) return false;
    while (days >= monthDays(year, month)) { days -= monthDays(year, month); ++month; }
    const unsigned seconds = static_cast<unsigned>(local % 86400);
    std::snprintf(out, size, "%04u%02u%02u_%02u%02u%02u", year, month, static_cast<unsigned>(days+1),
                  seconds/3600, seconds/60%60, seconds%60);
    return true;
}
}  // namespace geoputer

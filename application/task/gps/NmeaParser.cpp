#include "NmeaParser.h"
#include "GpsTime.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace geoputer {
namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool utcTime(const char* text, uint32_t& value) {
    const size_t size = std::strlen(text);
    if (size < 6) return false;
    for (size_t i = 0; i < 6; ++i) if (text[i] < '0' || text[i] > '9') return false;
    const unsigned hour = (text[0] - '0') * 10 + text[1] - '0';
    const unsigned minute = (text[2] - '0') * 10 + text[3] - '0';
    const unsigned second = (text[4] - '0') * 10 + text[5] - '0';
    if (hour > 23 || minute > 59 || second > 59) return false;
    unsigned fraction = 0;
    if (size > 6) {
        if (text[6] != '.' || size == 7) return false;
        unsigned scale = 100;
        for (size_t i = 7; i < size; ++i) {
            if (text[i] < '0' || text[i] > '9') return false;
            fraction += (text[i] - '0') * scale;
            scale /= 10;
        }
    }
    value = ((hour * 60 + minute) * 60 + second) * 1000 + fraction;
    return true;
}

bool optionalNumber(const char* text, double min, double max, float& value) {
    if (!text || !*text) return false;
    char* end = nullptr;
    const auto number = std::strtod(text, &end);
    if (!end || *end || !std::isfinite(number) || number < min || number > max) return false;
    value = static_cast<float>(number);
    return true;
}

bool coordinate(const char* text, const char* hemisphere, bool latitude, double& value) {
    const size_t degree_digits = latitude ? 2 : 3;
    const size_t size = std::strlen(text);
    if (size < degree_digits + 2 || std::strlen(hemisphere) != 1) return false;
    for (size_t i = 0; i < size; ++i) {
        if (i == degree_digits + 2 && text[i] == '.') {
            if (i + 1 == size) return false;
        } else if (text[i] < '0' || text[i] > '9' || i == degree_digits + 2) return false;
    }
    const char direction = hemisphere[0];
    if (latitude ? (direction != 'N' && direction != 'S') : (direction != 'E' && direction != 'W')) return false;
    unsigned degrees = 0;
    for (size_t i = 0; i < degree_digits; ++i) degrees = degrees * 10 + text[i] - '0';
    char* end = nullptr;
    const double minutes = std::strtod(text + degree_digits, &end);
    const unsigned limit = latitude ? 90 : 180;
    if (!end || *end || !std::isfinite(minutes) || minutes < 0 || minutes >= 60 ||
        degrees > limit || (degrees == limit && minutes != 0)) return false;
    value = degrees + minutes / 60;
    if (direction == 'S' || direction == 'W') value = -value;
    return true;
}

}  // namespace

NmeaResult NmeaParser::feed(char byte, GpsFix& position) {
    if (byte == '$') {
        collecting_ = true;
        length_ = 0;
        return NmeaResult::Incomplete;
    }
    if (!collecting_) return NmeaResult::Incomplete;
    if (byte == '\r') return NmeaResult::Incomplete;
    if (byte == '\n') {
        collecting_ = false;
        line_[length_] = '\0';
        return decode(position);
    }
    if (byte < 32 || byte > 126 || length_ + 1 >= sizeof(line_)) {
        collecting_ = false;
        return NmeaResult::Rejected;
    }
    line_[length_++] = byte;
    return NmeaResult::Incomplete;
}

NmeaResult NmeaParser::decode(GpsFix& position) {
    char* star = std::strchr(line_, '*');
    if (!star || std::strlen(star) != 3) return NmeaResult::Rejected;
    const int high = hexDigit(star[1]), low = hexDigit(star[2]);
    if (high < 0 || low < 0) return NmeaResult::Rejected;
    unsigned checksum = 0;
    for (char* p = line_; p != star; ++p) checksum ^= static_cast<unsigned char>(*p);
    if (checksum != static_cast<unsigned>((high << 4) | low)) return NmeaResult::Rejected;
    *star = '\0';
    if (star - line_ < 6 || line_[5] != ',') return NmeaResult::Rejected;
    // NMEA identifiers consist of a two-character talker plus a three-character type.
    for (unsigned i = 0; i < 5; ++i) if (line_[i] < 'A' || line_[i] > 'Z') return NmeaResult::Rejected;
    const bool gga = std::strncmp(line_ + 2, "GGA", 3) == 0;
    const bool rmc = std::strncmp(line_ + 2, "RMC", 3) == 0;
    if (!gga && !rmc) return NmeaResult::OtherSentence;
    char* fields[20]{};
    size_t count = 1;
    fields[0] = line_;
    for (char* p = line_; p != star; ++p) {
        if (*p == ',') {
            if (count == 20) return NmeaResult::Rejected;
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    if (count < 7) return NmeaResult::Rejected;
    const char* validity = fields[gga ? 6 : 2];
    if (std::strlen(validity) != 1 ||
        (gga ? (validity[0] < '0' || validity[0] > '8') : (validity[0] != 'A' && validity[0] != 'V'))) {
        return NmeaResult::Rejected;
    }
    position = {};
    position.utc_ms = UINT32_MAX;
    const bool has_time = utcTime(fields[1], position.utc_ms);
    const bool reported_valid = gga ? validity[0] != '0' : validity[0] == 'A';
    if (gga) {
        float satellites = 0;
        if (count > 7 && optionalNumber(fields[7], 0, 255, satellites) && std::floor(satellites) == satellites) {
            position.satellites = static_cast<uint8_t>(satellites); position.fields |= GpsSatellites;
        }
        if (count > 8 && optionalNumber(fields[8], 0, 655.35, position.hdop)) position.fields |= GpsHdop;
        if (reported_valid && count > 10 && std::strcmp(fields[10], "M") == 0 &&
            optionalNumber(fields[9], -2147483.648, 2147483.647, position.altitude_m)) position.fields |= GpsAltitude;
    } else {
        if (count > 9 && has_time && nmeaEpochMs(fields[9], position.utc_ms, position.utc_epoch_ms)) position.fields |= GpsTime;
        if (reported_valid && count > 7 && optionalNumber(fields[7], 0, 10000, position.speed_kmh)) {
            position.speed_kmh *= 1.852f; position.fields |= GpsSpeed;
        }
        if (reported_valid && count > 8 && optionalNumber(fields[8], 0, 359.9999, position.course_deg)) position.fields |= GpsCourse;
    }

    if (!reported_valid || (rmc && count > 12 && std::strcmp(fields[12], "N") == 0)) return NmeaResult::Position;
    if (!has_time ||
        !coordinate(fields[gga ? 2 : 3], fields[gga ? 3 : 4], true, position.latitude) ||
        !coordinate(fields[gga ? 4 : 5], fields[gga ? 5 : 6], false, position.longitude)) return NmeaResult::Rejected;
    position.valid = true;
    return NmeaResult::Position;
}

}  // namespace geoputer

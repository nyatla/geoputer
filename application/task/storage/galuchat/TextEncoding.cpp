#include "TextEncoding.h"
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace geoputer {
namespace {
const uint16_t Cp932[] = {
#include "Cp932Table.inc"
};

void appendUtf8(std::string& out, uint16_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

size_t charBytes(unsigned char c) {
    return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
}

// A UTF-8 boundary and a display-cell boundary are always kept together.
void copyLine(const std::string& value, char* out) {
    size_t bytes = 0, cells = 0;
    while (bytes < value.size() && cells < 14) {
        bytes += charBytes(static_cast<unsigned char>(value[bytes]));
        ++cells;
    }
    if (bytes < value.size()) {
        bytes = 0;
        for (unsigned i = 0; i < 11; ++i) bytes += charBytes(static_cast<unsigned char>(value[bytes]));
        std::memcpy(out, value.data(), bytes);
        std::memcpy(out + bytes, "...", 4);
    } else {
        std::memcpy(out, value.data(), bytes);
        out[bytes] = '\0';
    }
}
}  // namespace

std::string cp932ToUtf8(const std::string& source) {
    std::string result;
    result.reserve(source.size() * 2);
    for (size_t i = 0; i < source.size(); ++i) {
        const auto c = static_cast<unsigned char>(source[i]);
        uint16_t cp = 0;
        if (c < 0x80) cp = c;
        else if (c >= 0xA1 && c <= 0xDF) cp = 0xFF61 + c - 0xA1;
        else if (c == 0x80) cp = 0x80;
        else if (c == 0xA0) cp = 0xF8F0;
        else if (c >= 0xFD) cp = 0xF8F1 + c - 0xFD;
        else {
            const int lead = c >= 0x81 && c <= 0x9F ? c - 0x81 :
                             c >= 0xE0 && c <= 0xFC ? c - 0xE0 + 31 : -1;
            if (lead < 0 || i + 1 == source.size()) throw std::runtime_error("invalid CP932 lead byte");
            const auto t = static_cast<unsigned char>(source[++i]);
            const int trail = t >= 0x40 && t <= 0x7E ? t - 0x40 :
                              t >= 0x80 && t <= 0xFC ? t - 0x80 + 63 : -1;
            if (trail < 0 || !(cp = Cp932[lead * 188 + trail])) throw std::runtime_error("invalid CP932 pair");
        }
        if (!cp) throw std::runtime_error("NUL in place name");
        appendUtf8(result, cp);
    }
    return result;
}

bool validUtf8(const std::string& source) {
    for (size_t i = 0; i < source.size();) {
        const auto c = static_cast<unsigned char>(source[i++]);
        if (c > 0 && c < 0x80) continue;
        unsigned count; uint32_t cp, minimum;
        if (c >= 0xC2 && c <= 0xDF) { count = 1; cp = c & 31; minimum = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { count = 2; cp = c & 15; minimum = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { count = 3; cp = c & 7; minimum = 0x10000; }
        else return false;
        if (count > source.size() - i) return false;
        while (count--) {
            const auto next = static_cast<unsigned char>(source[i++]);
            if ((next & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (next & 63);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    }
    return true;
}

void formatPlaceLines(const std::vector<std::string>& components, char (&lines)[3][64]) {
    std::memset(lines, 0, sizeof(lines));
    std::string rows[3];
    if (!components.empty()) rows[0] = components[0];
    // Integrated: prefecture / county+city+ward / town+subdistrict.
    // N03: prefecture / remaining administrative components.
    for (size_t i = 1; i < components.size(); ++i) {
        if (!validUtf8(components[i])) throw std::runtime_error("invalid UTF-8 place name");
        rows[components.size() == 6 && i >= 4 ? 2 : 1] += components[i];
    }
    if (!validUtf8(rows[0])) throw std::runtime_error("invalid UTF-8 place name");
    for (unsigned i = 0; i < 3; ++i) copyLine(rows[i], lines[i]);
}

}  // namespace geoputer

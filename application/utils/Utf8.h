#pragma once
#include <cstdint>
#include <string>

namespace geoputer {
// Reject embedded NUL, overlong sequences, surrogates and values above U+10FFFF.
inline bool validUtf8(const std::string& source) {
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
} // namespace geoputer

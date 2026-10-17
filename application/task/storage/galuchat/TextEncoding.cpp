#include "TextEncoding.h"
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace geoputer {
namespace {
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

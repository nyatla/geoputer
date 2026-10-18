#include "TextEncoding.h"
#include "../../../PlaceText.h"
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace geoputer {
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

bool validPlaceFormat(const std::string& format) {
    if (!validUtf8(format)) return false;
    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') continue;
        if (++i == format.size() || (format[i] != '%' && (format[i] < '0' || format[i] > '9'))) return false;
    }
    return true;
}

void formatPlaceLines(const std::vector<std::string>& components, char (&lines)[2][64],
                      const PlaceDisplayFormat& format) {
    for (const auto& component : components)
        if (!validUtf8(component)) throw std::runtime_error("invalid UTF-8 place name");
    PlaceLineBuilder rows[2];
    if (format.specified) {
        for (unsigned row = 0; row < 2; ++row) {
            const auto& pattern = format.lines[row];
            if (!validPlaceFormat(pattern)) throw std::runtime_error("invalid place display format");
            for (size_t i = 0; i < pattern.size();) {
                if (pattern[i] == '%') {
                    const char token = pattern[i + 1]; i += 2;
                    if (token == '%') rows[row].append("%");
                    else if (static_cast<size_t>(token - '0') < components.size())
                        rows[row].append(components[token - '0'].c_str());
                } else {
                    const auto end = pattern.find('%', i);
                    const auto count = end == std::string::npos ? pattern.size() - i : end - i;
                    rows[row].append(pattern.data() + i, count); i += count;
                }
            }
        }
    } else {
        // Compatibility layout: prefecture + administrative names, then town.
        if (!components.empty()) rows[0].append(components[0].c_str());
        bool separator = !components.empty() && !components[0].empty();
        for (size_t i = 1; i < components.size(); ++i) {
            const unsigned row = components.size() == 6 && i >= 4 ? 1 : 0;
            if (!row && !components[i].empty() && separator) { rows[0].append(" "); separator = false; }
            rows[row].append(components[i].c_str());
        }
    }
    for (unsigned row = 0; row < 2; ++row) rows[row].finish(lines[row]);
}

}  // namespace geoputer

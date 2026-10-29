#pragma once
// Full SDK component formatting is an independent test oracle only.
#include "task/storage/galuchat/TextEncoding.h"
#include "PlaceText.h"
#include <string>
#include <vector>
#include <stdexcept>
namespace geoputer {
inline void formatPlaceLines(const std::vector<std::string>& components, char (&lines)[2][64],
                      const PlaceDisplayFormat& format = {}) {
    for (const auto& component : components)
        if (!validUtf8(component)) throw std::runtime_error("invalid UTF-8 place name");
    PlaceLineBuilder rows[2];
    if (format.specified) {
        for (unsigned row = 0; row < 2; ++row) {
            const auto pattern = format.line(row);
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

}

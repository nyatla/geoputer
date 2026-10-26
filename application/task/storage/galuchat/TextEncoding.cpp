#include "TextEncoding.h"
#include "../../../PlaceText.h"
#include <cstdint>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace geoputer {
namespace {
std::string_view metadataString(std::string_view metadata, std::string_view quoted_key) {
    const auto keyPos = metadata.find(quoted_key);
    if (keyPos == std::string_view::npos) return {};
    auto pos = metadata.find(':', keyPos);
    if (pos == std::string_view::npos) return {};
    do { ++pos; } while (pos < metadata.size() && std::isspace(static_cast<unsigned char>(metadata[pos])));
    if (pos >= metadata.size() || metadata[pos++] != '"') return {};
    const auto end = metadata.find('"', pos);
    return end == std::string_view::npos ? std::string_view{} : metadata.substr(pos, end - pos);
}
}
bool wordBookUsesUtf8(const std::string& metadata) {
    auto encoding=metadataString(metadata,"\"token_encoding\"");
    if (encoding.empty()) encoding=metadataString(metadata,"\"text_encoding\"");
    if (encoding.size()!=4 && encoding.size()!=5) return false;
    const char* expected=encoding.size()==4?"utf8":"utf-8";
    for (size_t i=0;i<encoding.size();++i)
        if (std::tolower(static_cast<unsigned char>(encoding[i]))!=expected[i]) return false;
    return true;
}

bool validPlaceFormat(std::string_view format) {
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

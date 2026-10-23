#pragma once

#include <string>
#include <vector>

namespace geoputer {

// Dictionary and display strings are UTF-8; no character-code conversion.
bool wordBookUsesUtf8(const std::string& metadata);
bool validUtf8(const std::string& source);
struct PlaceDisplayFormat {
    std::string lines[2];
    bool specified = false;
};
bool validPlaceFormat(const std::string& format);
void formatPlaceLines(const std::vector<std::string>& components, char (&lines)[2][64],
                      const PlaceDisplayFormat& format = {});

}  // namespace geoputer

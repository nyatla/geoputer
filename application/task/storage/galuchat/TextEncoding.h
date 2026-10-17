#pragma once

#include <string>
#include <vector>

namespace geoputer {

// Dictionary and display strings are UTF-8; no character-code conversion.
bool validUtf8(const std::string& source);
void formatPlaceLines(const std::vector<std::string>& components, char (&lines)[3][64]);

}  // namespace geoputer

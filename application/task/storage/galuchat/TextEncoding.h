#pragma once

#include <string>
#include <vector>

namespace geoputer {

// The SDK returns dictionary bytes unchanged. The display consumes UTF-8.
std::string cp932ToUtf8(const std::string& source);
bool validUtf8(const std::string& source);
void formatPlaceLines(const std::vector<std::string>& components, char (&lines)[3][64]);

}  // namespace geoputer

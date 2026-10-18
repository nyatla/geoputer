#pragma once

#include <cstddef>
#include <cstdint>

namespace geoputer {
size_t licenseLineCount();
const char* licenseLine(size_t index);
size_t licenseSectionCount();
size_t licenseSectionStart(size_t index);
}  // namespace geoputer

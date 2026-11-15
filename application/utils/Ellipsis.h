#pragma once

#include <cstddef>

namespace geoputer {

inline constexpr char EllipsisUtf8[] = "\xe2\x80\xa6";
inline constexpr size_t EllipsisBytes = 3;

inline bool isEllipsis(const char* text) {
    return static_cast<unsigned char>(text[0]) == 0xe2 &&
           static_cast<unsigned char>(text[1]) == 0x80 &&
           static_cast<unsigned char>(text[2]) == 0xa6;
}

inline size_t utf8CharacterBytes(unsigned char first) {
    return first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
}

} // namespace geoputer

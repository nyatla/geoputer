#pragma once
#include <string_view>
#include <galuchat/wgsmap3_reader.hpp>

namespace geoputer {
// Read the complete field, preserving the SDK's first-NUL comparison semantics.
inline bool matchesFixedAscii16(galuchat::ByteReader& source,std::string_view expected) {
    char bytes[16];
    for(auto& byte:bytes)byte=static_cast<char>(source.readByte());
    size_t length=0;while(length<sizeof(bytes) && bytes[length])++length;
    return std::string_view(bytes,length)==expected;
}
} // namespace geoputer

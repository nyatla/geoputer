#pragma once

#include "Ellipsis.h"
#include <cstring>

namespace geoputer {

inline const char* uiTextAt(const char* source,unsigned offset) {
    if(!source)return "";
    while(offset && *source){source+=utf8CharacterBytes(static_cast<unsigned char>(*source));--offset;}
    return source;
}

inline unsigned uiTextPixels(const char* source,bool japanese) {
    unsigned pixels=0;
    for(const char* text=source;text && *text;text+=utf8CharacterBytes(static_cast<unsigned char>(*text)))
        pixels+=japanese || (static_cast<unsigned char>(*text)>=128 && !isEllipsis(text))?16:8;
    return pixels;
}

inline bool uiTextCanScroll(const char* source,bool japanese,unsigned offset,unsigned max_pixels) {
    return uiTextPixels(uiTextAt(source,offset),japanese)>max_pixels;
}

// Clip UTF-8 text at a pixel boundary without allocating or splitting a code point.
inline void clipUiText(const char* source,bool japanese,char* out,size_t capacity,unsigned max_pixels) {
    if(!capacity)return;
    out[0]='\0';
    if(!source)return;
    size_t input=0,used=0,short_bytes=0;
    unsigned pixels=0;
    bool clipped=false;
    while(source[input]) {
        const auto first=static_cast<unsigned char>(source[input]);
        const size_t length=utf8CharacterBytes(first);
        const unsigned width=japanese || (first>=128 && !isEllipsis(source+input))?16:8;
        if(pixels+width>max_pixels || used+length>=capacity) {clipped=true;break;}
        std::memcpy(out+used,source+input,length);
        input+=length;used+=length;pixels+=width;
        if(pixels+(japanese?16:8)<=max_pixels && used+EllipsisBytes<capacity)short_bytes=used;
    }
    if(clipped && capacity>EllipsisBytes) {
        used=short_bytes;
        std::memcpy(out+used,EllipsisUtf8,EllipsisBytes);
        used+=EllipsisBytes;
    }
    out[used]='\0';
}

} // namespace geoputer

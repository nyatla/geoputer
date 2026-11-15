#pragma once

#include <cstddef>
#include <cstring>
#include <cstdint>
#include "Config.h"
#include "utils/Ellipsis.h"

namespace geoputer {

enum class PlaceTextMode : uint8_t { Japanese16, Ascii8x16 };

inline unsigned placeCharacterWidth(unsigned char first, PlaceTextMode mode = PlaceTextMode::Japanese16) {
    return first == ' ' || (mode == PlaceTextMode::Ascii8x16 && first < 0x80) ? 8 : 16;
}
inline unsigned placeEllipsisWidth(PlaceTextMode mode) { return mode == PlaceTextMode::Ascii8x16 ? 8 : 16; }
inline unsigned placeLogicalLinePixels() { return 2 * config::PlaceLinePixels; }

// Input has already been validated as UTF-8. Keep only the bounded display
// prefix, while remembering where a single-cell ellipsis will fit if needed.
class PlaceLineBuilder {
public:
    explicit PlaceLineBuilder(PlaceTextMode mode = PlaceTextMode::Japanese16,
                              unsigned max_pixels = config::PlaceLinePixels, bool compact = false)
        : mode_(mode), max_pixels_(max_pixels), compact_(compact) {}
    void append(const char* text, size_t size) {
        for (size_t i = 0; i < size && !clipped_;) {
            const auto first = static_cast<unsigned char>(text[i]);
            const size_t count = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            const unsigned width = compact_ ? 8 : isEllipsis(text + i) ? placeEllipsisWidth(mode_) : placeCharacterWidth(first, mode_);
            if (i + count > size || pixels_ + width > max_pixels_ || bytes_ + count >= sizeof(text_)) {
                clipped_ = true; break;
            }
            std::memcpy(text_ + bytes_, text + i, count);
            bytes_ += count; pixels_ += width; i += count;
            if (pixels_ + (compact_ ? 8 : placeEllipsisWidth(mode_)) <= max_pixels_ &&
                bytes_ + 3 < sizeof(text_)) short_bytes_ = bytes_;
        }
    }
    void append(const char* text) { append(text, std::strlen(text)); }
    void finish(char (&out)[config::PlaceCachedLineBytes]) const {
        const auto bytes = clipped_ ? short_bytes_ : bytes_;
        std::memset(out, 0, sizeof(out));
        std::memcpy(out, text_, bytes);
        if (clipped_) std::memcpy(out + bytes, EllipsisUtf8, EllipsisBytes);
    }
private:
    char text_[config::PlaceCachedLineBytes]{};
    size_t bytes_ = 0, short_bytes_ = 0;
    unsigned pixels_ = 0;
    bool clipped_ = false;
    PlaceTextMode mode_;
    unsigned max_pixels_;
    bool compact_;
};

inline void fitPlaceDisplayLine(const char* source, char (&out)[config::PlaceCachedLineBytes],
                                PlaceTextMode mode, bool compact = false) {
    PlaceLineBuilder line(mode, config::PlaceLinePixels, compact);
    line.append(source);
    line.finish(out);
}

inline bool asciiPlace(const char* text) {
    for (; *text; ++text) {
        if (isEllipsis(text)) { text += EllipsisBytes - 1; continue; }
        if (static_cast<unsigned char>(*text) >= 0x80) return false;
    }
    return true;
}

inline size_t asciiPlaceCells(const char* text) {
    size_t count=0;
    for(;*text;++count)text+=isEllipsis(text)?EllipsisBytes:1;
    return count;
}

inline bool liveAsciiWrapNeeded(const char (&lines)[2][config::PlaceCachedLineBytes], PlaceTextMode mode) {
    const size_t columns = config::PlaceLinePixels / 8;
    return mode == PlaceTextMode::Ascii8x16 && asciiPlace(lines[0]) && asciiPlace(lines[1]) &&
           (asciiPlaceCells(lines[0]) > columns || asciiPlaceCells(lines[1]) > columns);
}

inline void wrapAsciiPlaceLine(const char* source,
                              char (&rows)[2][config::PlaceLinePixels / 8 + EllipsisBytes]) {
    constexpr size_t columns = config::PlaceLinePixels / 8;
    std::memset(rows, 0, sizeof(rows));
    // ASCII words may end in a UTF-8 ellipsis; index cells instead of bytes.
    size_t offsets[config::PlaceCachedLineBytes]{};
    size_t length = 0;
    for (size_t byte = 0; source[byte] && length + 1 < config::PlaceCachedLineBytes;) {
        offsets[length++] = byte;
        byte += isEllipsis(source + byte) ? EllipsisBytes : 1;
        offsets[length] = byte;
    }
    if (length <= columns) {
        std::memcpy(rows[0], source, offsets[length]);
        return;
    }
    size_t split = columns;
    for (size_t i = columns; i > 0; --i) {
        if (source[offsets[i]] != ' ') continue;
        size_t after = i + 1;
        while (after < length && source[offsets[after]] == ' ') ++after;
        if (length - after <= columns) { split = i; break; }
    }
    std::memcpy(rows[0], source, offsets[split]);
    size_t second = split;
    while (second < length && source[offsets[second]] == ' ') ++second;
    const size_t remaining = length - second;
    if (remaining <= columns) std::memcpy(rows[1], source + offsets[second], offsets[length] - offsets[second]);
    else {
        const size_t kept = offsets[second + columns - 1] - offsets[second];
        std::memcpy(rows[1], source + offsets[second], kept);
        std::memcpy(rows[1] + kept, EllipsisUtf8, EllipsisBytes);
    }
}

static_assert(config::PlaceLinePixels >= 48, "Place line must fit an ellipsis");

}  // namespace geoputer

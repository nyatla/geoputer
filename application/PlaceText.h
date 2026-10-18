#pragma once

#include <cstddef>
#include <cstring>
#include "Config.h"

namespace geoputer {

inline unsigned placeCharacterWidth(unsigned char first) { return first == ' ' ? 8 : 16; }

// Input has already been validated as UTF-8. Keep only the bounded display
// prefix, while remembering where a three-dot ellipsis will fit if needed.
class PlaceLineBuilder {
public:
    void append(const char* text, size_t size) {
        for (size_t i = 0; i < size && !clipped_;) {
            const auto first = static_cast<unsigned char>(text[i]);
            const size_t count = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            const unsigned width = placeCharacterWidth(first);
            if (i + count > size || pixels_ + width > config::PlaceLinePixels || bytes_ + count >= sizeof(text_)) {
                clipped_ = true; break;
            }
            std::memcpy(text_ + bytes_, text + i, count);
            bytes_ += count; pixels_ += width; i += count;
            if (pixels_ + 48 <= config::PlaceLinePixels && bytes_ + 3 < sizeof(text_)) short_bytes_ = bytes_;
        }
    }
    void append(const char* text) { append(text, std::strlen(text)); }
    void finish(char (&out)[64]) const {
        const auto bytes = clipped_ ? short_bytes_ : bytes_;
        std::memset(out, 0, sizeof(out));
        std::memcpy(out, text_, bytes);
        if (clipped_) std::memcpy(out + bytes, "...", 3);
    }
private:
    char text_[64]{};
    size_t bytes_ = 0, short_bytes_ = 0;
    unsigned pixels_ = 0;
    bool clipped_ = false;
};

static_assert(config::PlaceLinePixels >= 48, "Place line must fit an ellipsis");

}  // namespace geoputer

#pragma once

#include <cstring>
#include <cstdio>
#include "UiTypes.h"

namespace geoputer {

inline bool hasPlace(const UiData& data) {
    return data.place[0][0] || data.place[1][0];
}

inline void beginPlacePresentation(UiData& data, const PlaceRequest& request) {
    // Keep the displayed name and its own coordinates together until completion.
    if (!hasPlace(data)) data.place_fix = request.fix;
    data.place_state = PlaceState::Searching;
}

// Storage has already validated UTF-8. Keep the final two display rows within
// fourteen native 16-dot cells, reserving three cells for the ellipsis.
inline void copyAddressLine(const char* source, char (&target)[64]) {
    size_t bytes = 0, short_bytes = 0;
    unsigned cells = 0;
    while (source[bytes] && cells < 14) {
        const auto first = static_cast<unsigned char>(source[bytes]);
        const unsigned count = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
        bool complete = true;
        for (unsigned i = 1; i < count; ++i) {
            if (!source[bytes+i] || (static_cast<unsigned char>(source[bytes+i]) & 0xc0) != 0x80) {
                complete = false; break;
            }
        }
        if (!complete) break;
        bytes += count;
        if (++cells == 11) short_bytes = bytes;
    }
    const bool truncate = source[bytes] && cells == 14;
    if (truncate) bytes = short_bytes;
    std::memset(target, 0, sizeof(target));
    std::memcpy(target, source, bytes);
    if (truncate) std::memcpy(target+bytes, "...", 3);
}

inline void formatLivePlaceLines(const char (&hierarchy)[3][64], char (&address)[2][64]) {
    char administrative[128];
    std::snprintf(administrative, sizeof(administrative), "%.*s%s%.*s",
                  63, hierarchy[0], hierarchy[0][0] && hierarchy[1][0] ? " " : "", 63, hierarchy[1]);
    copyAddressLine(administrative, address[0]);
    copyAddressLine(hierarchy[2], address[1]);
}

inline void completePlacePresentation(UiData& data, const PlaceResult& result) {
    formatLivePlaceLines(result.lines, data.place);
    data.place_fix = result.request.fix;
    data.place_state = result.state;
}

inline void clearPlacePresentation(UiData& data, PlaceState state) {
    std::memset(data.place, 0, sizeof(data.place));
    data.place_fix = {};
    data.place_state = state;
}

}  // namespace geoputer

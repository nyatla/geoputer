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

inline void completePlacePresentation(UiData& data, const PlaceResult& result) {
    std::memcpy(data.place, result.lines, sizeof(data.place));
    for (auto& line : data.place) line[sizeof(line) - 1] = '\0';
    data.place_fix = result.request.fix;
    data.place_state = result.state;
}

inline void clearPlacePresentation(UiData& data, PlaceState state) {
    std::memset(data.place, 0, sizeof(data.place));
    data.place_fix = {};
    data.place_state = state;
}

}  // namespace geoputer

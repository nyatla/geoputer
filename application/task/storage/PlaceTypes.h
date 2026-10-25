#pragma once

#include <cstdint>
#include "../gps/GpsTypes.h"

namespace geoputer {

enum class PlaceState : uint8_t {
    Waiting, Searching, Ready, NoFix, NoCard, DatasetMissing,
    NotFound, EncodingError, DataError, TooManyChunks, OutOfMemory, MetadataError, VersionError, Unselected
};

inline bool datasetUnavailable(PlaceState state) {
    return state == PlaceState::TooManyChunks || state == PlaceState::OutOfMemory ||
           state == PlaceState::DatasetMissing ||
           state == PlaceState::EncodingError || state == PlaceState::DataError ||
           state == PlaceState::MetadataError || state == PlaceState::VersionError || state == PlaceState::NoCard;
}

inline bool datasetInactive(PlaceState state) {
    return state == PlaceState::Unselected || datasetUnavailable(state);
}

struct DatasetFailure {
    uint32_t revision;
    uint8_t number;
    PlaceState reason;
    bool save_failed;
};

struct DatasetInfo {
    PlaceState state;
    uint64_t chunks;
    uint32_t limit;
    uint32_t generation = 1; // Explicit reopen only; errors retain the same generation.
    int unit_inv_x = 0, unit_inv_y = 0; // Validated dataset lattice; zero until opened.
};

struct PlaceRequest {
    uint32_t screen_generation;
    uint32_t sample_seq;
    GpsFix fix;
};

struct PlaceResult {
    PlaceRequest request;
    PlaceState state;
    int64_t region_code;
    char lines[2][64];  // Final UTF-8 display rows, bounded by pixel width.
    char detail[80];   // Diagnostic copy, never a pointer into the SDK.
    DatasetInfo dataset;
};

inline const char* placeStateName(PlaceState state) {
    switch (state) {
        case PlaceState::Unselected: return "DATASET UNSELECTED";
        case PlaceState::Waiting: return "WAITING";
        case PlaceState::Searching: return "SEARCHING...";
        case PlaceState::Ready: return "DATASET 01";
        case PlaceState::NoFix: return "WAIT GPS FIX";
        case PlaceState::NoCard: return "SD NOT READY";
        case PlaceState::DatasetMissing: return "DATASET 01 MISSING";
        case PlaceState::NotFound: return "PLACE NOT FOUND";
        case PlaceState::EncodingError: return "ENCODING ERROR";
        case PlaceState::MetadataError: return "DATASET CONFIG ERROR";
        case PlaceState::VersionError: return "DATASET BAD VERSION";
        case PlaceState::DataError: return "DATA READ ERROR";
        case PlaceState::TooManyChunks: return "DATASET TOO LARGE";
        case PlaceState::OutOfMemory: return "DATASET NO MEMORY";
    }
    return "DATA READ ERROR";
}

}  // namespace geoputer

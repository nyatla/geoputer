#pragma once

#include <cstdint>
#include "../gps/GpsTypes.h"

namespace geoputer {

enum class PlaceState : uint8_t {
    Waiting, Searching, Ready, NoFix, NoCard, DatasetMissing, AmbiguousFiles,
    NotFound, EncodingError, DataError, TooManyChunks, OutOfMemory
};

inline bool datasetUnavailable(PlaceState state) {
    return state == PlaceState::TooManyChunks || state == PlaceState::OutOfMemory;
}

struct DatasetInfo {
    PlaceState state;
    uint64_t chunks;
    uint32_t limit;
};

enum class PlaceAccessMode : uint8_t { Cache, Reload };

struct PlaceAccess {
    uint32_t request_id;
    uint32_t screen_generation;
    PlaceAccessMode mode;
    bool active;
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
        case PlaceState::Waiting: return "WAITING";
        case PlaceState::Searching: return "SEARCHING...";
        case PlaceState::Ready: return "DATASET 01";
        case PlaceState::NoFix: return "WAIT GPS FIX";
        case PlaceState::NoCard: return "SD NOT READY";
        case PlaceState::DatasetMissing: return "DATASET 01 MISSING";
        case PlaceState::AmbiguousFiles: return "MULTIPLE DATA FILES";
        case PlaceState::NotFound: return "PLACE NOT FOUND";
        case PlaceState::EncodingError: return "ENCODING ERROR";
        case PlaceState::DataError: return "DATA READ ERROR";
        case PlaceState::TooManyChunks: return "DATASET TOO LARGE";
        case PlaceState::OutOfMemory: return "DATASET NO MEMORY";
    }
    return "DATA READ ERROR";
}

}  // namespace geoputer

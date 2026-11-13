#pragma once

#include <cstdint>
#include <cstddef>
#include "../../map/MapTypes.h"
#include "../../Config.h"
#include "../../PlaceText.h"
#include "../../alarm/AlarmTypes.h"
#include "../gps/GpsTypes.h"
#include "ReadWorkspace.h"
#include "galuchat/WordBookWorkspace.h"

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
    PlaceTextMode place_text_mode = PlaceTextMode::Japanese16; // Resolved Wordbook language, not UI language.
};

struct PlaceRequest {
    uint32_t screen_generation;
    uint32_t sample_seq;
    GpsFix fix;
    uint32_t token = 0, dataset_generation = 0;
    bool alarm_sample=false;
    uint32_t alarm_revision=0;
    map::BufferLease buffer{};
    size_t work_bytes = 0;
};

struct PlaceResult {
    PlaceRequest request;
    PlaceState state;
    int64_t region_code;
    char lines[2][config::PlaceResultLineBytes]; // Bounded logical rows; UI decides screen clipping.
    char detail[80];   // Diagnostic copy, never a pointer into the SDK.
    DatasetInfo dataset;
    alarm::Observation alarm{};
};

// The reply is first; only it is initialized. Scratch lives after the reply,
// and is returned with the same UI loan after App consumes the completion.
struct PlaceReadWorkspace {
    PlaceResult result{};
    ReaderWorkspaceBytes reader;
    WordBookWorkspace wordbook;
};
inline bool validPlaceMemory(const PlaceRequest& request) {
    return request.token && map::validBuffer(request.buffer) && request.work_bytes==sizeof(ReaderWorkspaceBytes) &&
        BorrowedLayout<PlaceReadWorkspace>::fits(request.buffer.data,request.buffer.capacity);
}
inline PlaceResult* beginPlaceMemory(const PlaceRequest& request) {
    return &BorrowedLayout<PlaceReadWorkspace>::construct(request.buffer.data,request.buffer.capacity)->result;
}
inline void endPlaceMemory(const PlaceRequest& request) {
    BorrowedLayout<PlaceReadWorkspace>::destroy(request.buffer.data,request.buffer.capacity);
}
inline PlaceResult* placeResultMemory(const PlaceRequest& request) {
    return &BorrowedLayout<PlaceReadWorkspace>::get(request.buffer.data,request.buffer.capacity)->result;
}
inline ReaderWorkspaceBytes& placeReaderMemory(const PlaceRequest& request) {
    return BorrowedLayout<PlaceReadWorkspace>::get(request.buffer.data,request.buffer.capacity)->reader;
}
inline WordBookWorkspace& placeWordBookMemory(const PlaceRequest& request) {
    return BorrowedLayout<PlaceReadWorkspace>::get(request.buffer.data,request.buffer.capacity)->wordbook;
}
static_assert(offsetof(PlaceReadWorkspace,result)==0,"Reply must start at the UI loan address");
static_assert(offsetof(PlaceReadWorkspace,reader)>=sizeof(PlaceResult),"Scratch follows the reply");
static_assert(sizeof(PlaceReadWorkspace)<=map::BoundaryBytes,"Place scratch and result must fit the UI back image");
inline bool samePlaceRequest(const PlaceRequest& a,const PlaceRequest& b) {
    return a.token==b.token && a.screen_generation==b.screen_generation && a.sample_seq==b.sample_seq &&
        a.dataset_generation==b.dataset_generation && a.alarm_sample==b.alarm_sample && a.alarm_revision==b.alarm_revision && a.work_bytes==b.work_bytes && map::sameBuffer(a.buffer,b.buffer);
}


inline const char* placeStateName(PlaceState state) {
    switch (state) {
        case PlaceState::Unselected: return "DATASET UNSELECTED";
        case PlaceState::Waiting: return "WAITING";
        case PlaceState::Searching: return "SEARCHING...";
        case PlaceState::Ready: return "DATASET READY";
        case PlaceState::NoFix: return "WAIT GPS FIX";
        case PlaceState::NoCard: return "SD NOT READY";
        case PlaceState::DatasetMissing: return "DATASET MISSING";
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

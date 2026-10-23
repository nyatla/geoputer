#pragma once

#include <cstring>
#include <cstdio>
#include "UiTypes.h"

namespace geoputer {

inline const char* mainError(const UiData& data) {
    if (data.storage.log.error != LogError::None) return logErrorName(data.storage.log.error);
    if (data.storage.log_result == LogError::Io || data.storage.log_result == LogError::BufferFull || data.storage.log_result == LogError::Capacity)
        return logErrorName(data.storage.log_result);
    if (data.storage.dataset_failure.save_failed) return "DATASET CLEAR SAVE FAILED";
    if (data.storage.card_state != CardState::Mounted) return "SD NOT READY";
    if (data.gps.reception == GpsReception::Unavailable) return "GPS UNAVAILABLE";
    if (data.gps.reception == GpsReception::NoData) return "GPS SIGNAL LOST";
    if (!data.gps.fix.valid) return "GPS WAITING FOR FIX";
    return nullptr;
}
inline const char* datasetFailureLabel(PlaceState state) {
    switch(state) {
        case PlaceState::NoCard:return "NO SD";
        case PlaceState::DatasetMissing:return "MISSING";
        case PlaceState::AmbiguousFiles:case PlaceState::MetadataError:return "CONFIG";
        case PlaceState::VersionError:return "VERSION";
        case PlaceState::EncodingError:return "ENCODING";
        case PlaceState::TooManyChunks:return "TOO LARGE";
        case PlaceState::OutOfMemory:return "NO MEMORY";
        default:return "READ ERROR";
    }
}
inline void formatDatasetRelease(const DatasetFailure& failure,char* out,size_t size) {
    std::snprintf(out,size,"DS%02u %s -> NONE",failure.number,datasetFailureLabel(failure.reason));
}
struct DatasetIssueText { const char* first; const char* second; };
inline DatasetIssueText datasetIssueText(PlaceState state, bool ja) {
    switch (state) {
        case PlaceState::Unselected: return ja ? DatasetIssueText{"データセット", "未選択"} : DatasetIssueText{"DATASET", "UNSELECTED"};
        case PlaceState::NoCard: return ja ? DatasetIssueText{"SDカードが","利用できません"} : DatasetIssueText{"SD CARD","NOT READY"};
        case PlaceState::DatasetMissing: return ja ? DatasetIssueText{"データセットが","見つかりません"} : DatasetIssueText{"DATASET","NOT FOUND"};
        case PlaceState::AmbiguousFiles: case PlaceState::MetadataError:
            return ja ? DatasetIssueText{"データセットの","構成が不正です"} : DatasetIssueText{"DATASET","CONFIG ERROR"};
        case PlaceState::VersionError: return ja ? DatasetIssueText{"データセットの","形式に未対応です"} : DatasetIssueText{"DATASET","BAD VERSION"};
        case PlaceState::EncodingError: return ja ? DatasetIssueText{"データセットの","文字コードが未対応です"} : DatasetIssueText{"WORDBOOK","BAD ENCODING"};
        case PlaceState::TooManyChunks: return ja ? DatasetIssueText{"チャンク数が","上限を超えています"} : DatasetIssueText{"MAP CHUNKS","OVER LIMIT"};
        case PlaceState::OutOfMemory: return ja ? DatasetIssueText{"地図読込用の","メモリが不足しています"} : DatasetIssueText{"MAP MEMORY","NOT ENOUGH"};
        default: return ja ? DatasetIssueText{"地図データを","読み込めません"} : DatasetIssueText{"MAP DATA","READ ERROR"};
    }
}
inline PlaceState placeIssue(const UiData& data) {
    if (data.dataset.state == PlaceState::Unselected) return PlaceState::Unselected;
    if (data.storage.card_state != CardState::Mounted) return PlaceState::NoCard;
    if (datasetInactive(data.dataset.state)) return data.dataset.state;
    if (datasetInactive(data.place_state)) return data.place_state;
    // Non-dataset errors still offer ESC without being rendered in the name area.
    return mainError(data) ? PlaceState::NoFix : PlaceState::Ready;
}
inline bool applyDatasetInfo(DatasetInfo& current, const DatasetInfo& incoming) {
    if (current.generation && static_cast<int32_t>(incoming.generation - current.generation) < 0) return false;
    if (current.generation == incoming.generation && datasetInactive(current.state) && !datasetInactive(incoming.state)) return false;
    current = incoming;
    return true;
}

inline bool hasPlace(const UiData& data) {
    return data.place[0][0] || data.place[1][0];
}

inline void beginPlacePresentation(UiData& data, const PlaceRequest& request) {
    // Keep the displayed name and its own coordinates together until completion.
    if (!hasPlace(data)) data.place_fix = request.fix;
    data.place_state = PlaceState::Searching;
}

inline void completePlacePresentation(UiData& data, const PlaceResult& result) {
    if (datasetInactive(result.state)) {
        std::memset(data.place, 0, sizeof(data.place)); data.place_fix = {}; data.place_state = result.state; return;
    }
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

#pragma once
#include "../../../Config.h"
#include "../../../DatasetId.h"
#include "../log/LogTypes.h"
#include "../PlaceTypes.h"
#include "../../../map/MapTypes.h"

namespace geoputer {
enum class DatasetPurpose : uint8_t { Select, Rebuild, Verify };
enum class DatasetAction : uint8_t { Open, Older, Newer, Detail, Apply, RebuildIndex, VerifyIndex, Clear };
enum class DatasetError : uint8_t { None, Missing, Metadata, Version, Data, Encoding, TooManyChunks, NoMemory, NotStopped, SaveFailed, IndexFailed, IndexRestoreFailed, Busy, IndexMismatch, Unselected, CardUnavailable, AlreadyUnselected, SettingsUnavailable };
enum class IndexPhase : uint8_t { Idle, Preparing, Map, TI, TT, TM, Structure, Publish, Complete, Failed };
struct IndexProgress {
    uint32_t token, elapsed_ms;
    IndexPhase phase;
    uint8_t percent;
    bool verifying;
    char language[config::DatasetLanguageBytes];
};
using IndexProgressCallback=void (*)(void*,const IndexProgress&);
struct DatasetRequest {
    DatasetAction action;
    DatasetPurpose purpose;
    uint32_t token;
    DatasetId id;
    uint16_t page;
    bool reset_settings;
    char language[config::DatasetLanguageBytes];
    uint32_t screen_generation=0;
    map::BufferLease buffer{};
};
struct DatasetRow { DatasetId id; DatasetError error; };
struct DatasetDetail {
    char title[config::DatasetLineBytes], description[config::DatasetLineBytes], language[config::DatasetLanguageBytes];
    char wordbook_types[config::MaxDatasetLanguages][config::DatasetLanguageBytes];
    double west, south, east, north;
    uint64_t chunks;
    uint32_t unit_inv_x, unit_inv_y;
    uint16_t language_index, language_count;
};
struct DatasetView {
    uint32_t revision;
    DatasetId id;
    uint16_t page;
    uint8_t count, selected;
    bool detail, loading, applying, has_older, has_newer;
    DatasetError error;
    DatasetPurpose purpose;
    IndexProgress progress;
    union { DatasetRow rows[config::DatasetPageRows]; DatasetDetail info; };
};
struct DatasetCompletion {
    DatasetRequest request;
    DatasetError error;
};
inline bool datasetMutation(DatasetAction action) {
    return action==DatasetAction::Apply || action==DatasetAction::Clear ||
        action==DatasetAction::RebuildIndex || action==DatasetAction::VerifyIndex;
}
inline bool sameDatasetRequest(const DatasetRequest& a,const DatasetRequest& b) {
    return a.token && a.token==b.token && a.screen_generation==b.screen_generation &&
        a.action==b.action && a.purpose==b.purpose && a.id==b.id && a.page==b.page && map::sameBuffer(a.buffer,b.buffer);
}
inline bool datasetLogStopped(const RecordingSnapshot& recording,const LogSnapshot& log) {
    return loggerStopped(recording, log);
}
inline bool datasetRecoverable(DatasetError error) {
    return error==DatasetError::None || error==DatasetError::NotStopped || error==DatasetError::SaveFailed || error==DatasetError::IndexFailed || error==DatasetError::IndexRestoreFailed;
}

inline PlaceState datasetErrorState(DatasetError error) {
    switch(error) {
        case DatasetError::Missing:return PlaceState::DatasetMissing;
        case DatasetError::Metadata:return PlaceState::MetadataError;
        case DatasetError::Version:return PlaceState::VersionError;
        case DatasetError::Encoding:return PlaceState::EncodingError;
        case DatasetError::TooManyChunks:return PlaceState::TooManyChunks;
        case DatasetError::NoMemory:return PlaceState::OutOfMemory;
        case DatasetError::CardUnavailable:return PlaceState::NoCard;
        default:return PlaceState::DataError;
    }
}
inline const char* datasetErrorText(DatasetError error,bool ja) {
    switch(error) {
        case DatasetError::None:return "";
        case DatasetError::SettingsUnavailable:return ja?"設定を保存できません":"Settings unavailable";
        case DatasetError::CardUnavailable:return ja?"SDカードが利用できません":"SD card not ready";
        case DatasetError::AlreadyUnselected:return ja?"すでに未選択です":"Already unselected";
        case DatasetError::Unselected:return ja?"データセットが未選択です":"No dataset selected";
        case DatasetError::Missing:return ja?"データセットが見つかりません":"Dataset not found";
        case DatasetError::Metadata:return ja?"メタデータが不正です":"Invalid metadata";
        case DatasetError::Version:return ja?"未対応の形式です":"Unsupported version";
        case DatasetError::TooManyChunks:return ja?"チャンク数が上限を超えています":"Too many chunks";
        case DatasetError::NoMemory:return ja?"メモリが不足しています":"Not enough memory";
        case DatasetError::Encoding:return ja?"文字コードに対応していません":"Unsupported encoding";
        case DatasetError::NotStopped:return ja?"先にログを停止してください":"Stop logger first";
        case DatasetError::SaveFailed:return ja?"設定の保存に失敗しました":"Settings save failed";
        case DatasetError::IndexFailed:return ja?"キャッシュの作成に失敗":"Cache creation failed";
        case DatasetError::IndexRestoreFailed:return ja?"キャッシュの復元に失敗":"Cache restore failed";
        case DatasetError::IndexMismatch:return ja?"キャッシュが一致しません":"Cache does not match";
        case DatasetError::Busy:return ja?"処理中です":"Working...";
        default:return ja?"データを読み込めません":"Data read failed";
    }
}
} // namespace geoputer

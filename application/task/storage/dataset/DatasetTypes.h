#pragma once
#include "../../../Config.h"
#include "../log/LogTypes.h"

namespace geoputer {
enum class DatasetAction : uint8_t { Open, Older, Newer, Detail, Language, Apply, CancelLanguage };
enum class DatasetError : uint8_t { None, Missing, Metadata, Version, Data, Encoding, TooManyChunks, NoMemory, NotStopped, SaveFailed, Busy };
struct DatasetRequest {
    DatasetAction action;
    uint32_t token;
    uint8_t number;
    int8_t direction;
    bool reset_settings;
    char language[config::DatasetLanguageBytes];
};
struct DatasetRow { uint8_t number; char title[64]; };
struct DatasetDetail {
    char title[64], description[256], language[config::DatasetLanguageBytes];
    double west, south, east, north;
    uint64_t chunks;
    uint32_t unit_inv_x, unit_inv_y;
    uint16_t language_index, language_count;
};
struct DatasetView {
    uint32_t revision;
    uint8_t number, count, selected;
    bool detail, loading, applying, has_older, has_newer, restart_required;
    DatasetError error;
    union { DatasetRow rows[config::DatasetPageRows]; DatasetDetail info; };
};
inline bool datasetLogStopped(const RecordingSnapshot& recording,const LogSnapshot& log) {
    return recording.state==RecordingState::Stopped && !recording.pending && !log.open && !log.closing && !log.buffered;
}
inline const char* datasetErrorText(DatasetError error,bool ja) {
    switch(error) {
        case DatasetError::None:return "";
        case DatasetError::Missing:return ja?"データセットが見つかりません":"Dataset not found";
        case DatasetError::Metadata:return ja?"メタデータが不正です":"Invalid metadata";
        case DatasetError::Version:return ja?"未対応の形式です":"Unsupported version";
        case DatasetError::TooManyChunks:return ja?"チャンク数が上限を超えています":"Too many chunks";
        case DatasetError::NoMemory:return ja?"メモリが不足しています":"Not enough memory";
        case DatasetError::Encoding:return ja?"文字コードに対応していません":"Unsupported encoding";
        case DatasetError::NotStopped:return ja?"先にログを停止してください":"Stop logger first";
        case DatasetError::SaveFailed:return ja?"設定の保存に失敗しました":"Settings save failed";
        case DatasetError::Busy:return ja?"処理中です":"Working...";
        default:return ja?"データを読み込めません":"Data read failed";
    }
}
} // namespace geoputer

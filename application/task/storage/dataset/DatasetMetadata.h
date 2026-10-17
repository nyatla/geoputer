#pragma once
#include <string>
#include <vector>
#include "DatasetTypes.h"
#include "../../../Settings.h"
#include "../StorageActivity.h"

namespace geoputer {
struct DatasetMetadata {
    std::string map, wordbook, title, description, default_language, language;
    std::vector<std::string> languages; // IDs only; no tree of all translations.
    DatasetError error=DatasetError::None;
};
DatasetMetadata readDatasetMetadata(const std::string& directory,UiLanguage ui,const char* language="", StorageObserver* observer=nullptr);
void datasetCopyText(char* target,size_t capacity,const std::string& source);
} // namespace geoputer

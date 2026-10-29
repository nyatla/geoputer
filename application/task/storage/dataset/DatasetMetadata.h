#pragma once
#include <array>
#include <string_view>
#include "DatasetTypes.h"
#include "../../../Settings.h"
#include "../StorageActivity.h"
#include "../galuchat/TextEncoding.h"

namespace geoputer {
// Short-lived execution result. No UI strings or retained language list.
struct DatasetMetadata {
    char map[config::DatasetFilenameBytes]{},wordbook[config::DatasetFilenameBytes]{};
    char language[config::DatasetLanguageBytes]{};
    PlaceDisplayFormat display;
    uint16_t language_count=0,language_index=0;
    DatasetError error=DatasetError::None;
};
struct DatasetMetadataText {
    char* title=nullptr;size_t title_capacity=0;
    char* description=nullptr;size_t description_capacity=0;
};
// The parser normally keeps this on its own stack. Index maintenance can ask
// for a temporary copy to enumerate all languages; it is never a session field.
struct DatasetLanguages {
    std::array<std::array<char,config::DatasetLanguageBytes>,config::MaxDatasetLanguages> ids{};
    uint8_t count=0;
    size_t size()const {return count;}
    unsigned find(std::string_view language)const {
        for(unsigned i=0;i<count;++i)if(language==ids[i].data())return i;
        return count;
    }
};
static_assert(sizeof(DatasetLanguages::ids)==512,"Sixteen terminated language IDs");
[[gnu::noinline]] void readDatasetMetadataInto(std::string_view directory,UiLanguage ui,const char* language,
    DatasetMetadata& result,StorageObserver* observer=nullptr,DatasetMetadataText text={},
    int direction=0,DatasetLanguages* languages=nullptr);
// Value-return convenience for non-UI callers; the result is fixed-size.
inline DatasetMetadata readDatasetMetadata(std::string_view directory,UiLanguage ui,const char* language="",StorageObserver* observer=nullptr) {
    DatasetMetadata result;readDatasetMetadataInto(directory,ui,language,result,observer);return result;
}
void datasetCopyText(char* target,size_t capacity,std::string_view source);
} // namespace geoputer

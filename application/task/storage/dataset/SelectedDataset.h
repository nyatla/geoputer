#pragma once
#include "DatasetIndex.h"
#include "../PlaceTypes.h"
#include "../StoragePath.h"
#include "../galuchat/TextEncoding.h"
#include <cstring>
#include <string_view>

namespace geoputer {
enum class DatasetFile : uint8_t {Map,Wordbook,Index};
// Storage owns and updates this stable session. Lookup and map readers borrow
// it; all consumers close before names change. App receives completion snapshots.
struct SelectedDataset {
    SelectedDataset()=default;
    SelectedDataset(const SelectedDataset&)=delete;
    SelectedDataset& operator=(const SelectedDataset&)=delete;
    uint8_t number=0;
    char language[config::DatasetLanguageBytes]{};
    char map_name[config::DatasetFilenameBytes]{},wordbook_name[config::DatasetFilenameBytes]{};
    PlaceDisplayFormat display;
    DatasetMapIndex map_index{};
    DatasetWordBookIndex book_index{};
    DatasetInfo info{PlaceState::Unselected,0,config::MaxDatasetChunks,1};
    bool ready()const {return info.state==PlaceState::Ready;}
    bool setLanguage(std::string_view value) {
        if(value.size()>=sizeof(language) || value.find('\0')!=std::string_view::npos)return false;
        if(!value.empty())std::memmove(language,value.data(),value.size());
        language[value.size()]='\0';return true;
    }
    bool setNames(std::string_view map,std::string_view wordbook) {
        if(map.empty() || wordbook.empty() || map.size()>=sizeof(map_name) || wordbook.size()>=sizeof(wordbook_name) ||
           map.find('\0')!=std::string_view::npos || wordbook.find('\0')!=std::string_view::npos)return false;
        std::memcpy(map_name,map.data(),map.size());map_name[map.size()]='\0';
        std::memcpy(wordbook_name,wordbook.data(),wordbook.size());wordbook_name[wordbook.size()]='\0';return true;
    }
    FileReadPath file(DatasetFile kind,std::string_view root=config::DatasetRoot)const {
        const char* name=kind==DatasetFile::Map?map_name:kind==DatasetFile::Wordbook?wordbook_name:"_mapindex.dat";
        return FileReadPath::dataset(number,name,root);
    }
    void clearPrepared() {
        map_name[0]=wordbook_name[0]='\0';display={};map_index={};book_index={};
    }
};
} // namespace geoputer

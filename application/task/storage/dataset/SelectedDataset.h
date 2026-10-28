#pragma once
#include "DatasetIndex.h"
#include "../PlaceTypes.h"
#include "../galuchat/TextEncoding.h"
#include <string>

namespace geoputer {
// Storage owns one runtime selection. Consumers borrow it; browsing another
// dataset never changes it. Paths remain stable until all consumers are reset.
struct SelectedDataset {
    SelectedDataset()=default;
    SelectedDataset(const SelectedDataset&)=delete;
    SelectedDataset& operator=(const SelectedDataset&)=delete;
    std::string directory, language;
    std::string map_path, index_path, wordbook_path;
    PlaceDisplayFormat display;
    DatasetMapIndex map_index{};
    DatasetWordBookIndex book_index{};
    DatasetInfo info{PlaceState::Unselected,0,config::MaxDatasetChunks,1};

    bool ready()const {return info.state==PlaceState::Ready;}
    // Call only after releasing Readers and factories borrowing these strings.
    void clearPrepared() {
        map_path.clear();index_path.clear();wordbook_path.clear();
        display={};map_index={};book_index={};
    }
};
} // namespace geoputer

#pragma once
#include "DatasetMetadata.h"
#include <string>
#include <type_traits>
#include "../StoragePath.h"
namespace geoputer {
class ReadFileSlots;
class DatasetIndexWork;
class DatasetCatalog {
public:
    explicit DatasetCatalog(std::string_view root=config::DatasetRoot, StorageObserver* observer=nullptr,uint8_t drive=0,ReadFileSlots* files=nullptr,DatasetIndexWork* work=nullptr):root_(root),observer_(observer),drive_(drive),files_(files),work_(work) {}
    template<class T,class... Args, std::enable_if_t<std::is_same_v<std::decay_t<T>,std::string> && !std::is_lvalue_reference_v<T>,int> = 0>
    DatasetCatalog(T&&,Args&&...)=delete;
    DatasetView read(const DatasetRequest& request,UiLanguage ui) const;
    void readInto(const DatasetRequest& request,UiLanguage ui,DatasetView& view) const;
    TemporaryPath directory(unsigned number) const;
private:
    std::string_view root_; // ROM/caller-owned; must outlive the catalog.
    StorageObserver* observer_;
    uint8_t drive_;
    ReadFileSlots* files_;
    DatasetIndexWork* work_;
};
} // namespace geoputer

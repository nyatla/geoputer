#pragma once
#include "DatasetMetadata.h"
namespace geoputer {
class DatasetCatalog {
public:
    explicit DatasetCatalog(std::string root="/sd/dataset", StorageObserver* observer=nullptr,uint8_t drive=0):root_(std::move(root)),observer_(observer),drive_(drive) {}
    DatasetView read(const DatasetRequest& request,UiLanguage ui) const;
    std::string directory(unsigned number) const;
private:
    std::string root_;
    StorageObserver* observer_;
    uint8_t drive_;
};
} // namespace geoputer

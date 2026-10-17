#pragma once
#include "DatasetMetadata.h"
namespace geoputer {
class DatasetCatalog {
public:
    explicit DatasetCatalog(std::string root="/sd/dataset", StorageObserver* observer=nullptr):root_(std::move(root)),observer_(observer) {}
    DatasetView read(const DatasetRequest& request,UiLanguage ui) const;
    std::string directory(unsigned number) const;
private:
    std::string root_;
    StorageObserver* observer_;
};
} // namespace geoputer

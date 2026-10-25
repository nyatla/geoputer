#pragma once

#include <memory>
#include <string>
#include "DiskIndexedWordBookReader.h"
#include "CachedMapReader.h"
#include "WordBookCache.h"
#include "TextEncoding.h"
#include "ReadTiming.h"
#include "../PlaceTypes.h"
#include "../dataset/DatasetTypes.h"

namespace geoputer {

// Owned and called exclusively by StorageTask. No file handles cross task queues.
class PlaceLookup {
public:
    explicit PlaceLookup(std::string directory = {}) : directory_(std::move(directory)) {
        dataset_.state = directory_.empty() ? PlaceState::Unselected : PlaceState::Waiting;
    }
    PlaceLookup(const PlaceLookup&) = delete;
    PlaceLookup& operator=(const PlaceLookup&) = delete;
    void setObserver(StorageObserver* observer) { observer_ = observer; }
    PlaceResult search(const PlaceRequest& request);
    void configure(std::string directory, std::string language = ""); // Empty directory disables lookup without file I/O.
    void setDrive(uint8_t drive) { drive_=drive; }
    const std::string& selectedLanguage() const { return language_; }
    PlaceResult prepare();  // Validate file pair/headers/encoding without requiring GPS.
    void close(); // Explicit revalidation boundary; clears a latched failure.
    DatasetInfo datasetInfo() const { return dataset_; }
    MapCacheStats cacheStats() const { return map_ ? map_->stats() : MapCacheStats{}; }
    WordBookCacheStats wordBookCacheStats() const { return wordbook_cache_.stats(); }
    const PlaceReadTiming& readTiming() const { return timing_; }
private:
    PlaceState open();
    void releaseReaders();
    PlaceState reject(PlaceState state);
    std::string directory_;
    std::string language_;
    PlaceDisplayFormat display_;
    uint8_t drive_=0;
    PlaceReadTiming timing_{}; // Must outlive the Readers that refer to its I/O counters.
    std::unique_ptr<CachedMapReader> map_;
    std::unique_ptr<DiskIndexedWordBookReader> wordbook_;
    WordBookCache wordbook_cache_;
    StorageObserver* observer_ = nullptr;
    DatasetInfo dataset_{};
};

}  // namespace geoputer

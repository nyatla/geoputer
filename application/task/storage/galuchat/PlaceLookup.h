#pragma once

#include <memory>
#include <optional>
#include <string>
#include "DiskIndexedWordBookReader.h"
#include "CachedMapReader.h"
#include "WordBookCache.h"
#include "TextEncoding.h"
#include "ReadTiming.h"
#include "../PlaceTypes.h"
#include "../dataset/DatasetTypes.h"
#include "../dataset/SelectedDataset.h"

namespace geoputer {
class BorrowedReadBuffer;
class ObservedFileReaderFactory;

// Owned and called exclusively by StorageTask. No file handles cross task queues.
class PlaceLookup {
public:
    PlaceLookup(CodeCacheBuffer& pixels,SelectedDataset& selected,std::string directory = {})
        :pixels_(pixels),selected_(selected) {
        selected_.directory=std::move(directory);
        selected_.info.state=selected_.directory.empty()?PlaceState::Unselected:PlaceState::Waiting;
    }
    PlaceLookup(const PlaceLookup&) = delete;
    PlaceLookup& operator=(const PlaceLookup&) = delete;
    void setObserver(StorageObserver* observer) { observer_ = observer; }
    PlaceResult search(const PlaceRequest& request);
    void searchInto(const PlaceRequest& request,PlaceResult& result,BorrowedReadBuffer* work=nullptr);
    void configure(std::string directory, std::string language = ""); // Empty directory disables lookup without file I/O.
    void setDrive(uint8_t drive) { drive_=drive; }
    const std::string& selectedLanguage() const { return selected_.language; }
    PlaceResult prepare();  // Validate file pair/headers/encoding without requiring GPS.
    void close(); // Explicit revalidation boundary; clears a latched failure.
    DatasetInfo datasetInfo() const { return selected_.info; }
    MapCacheStats cacheStats() const { return map_ ? map_->stats() : MapCacheStats{}; }
    WordBookCacheStats wordBookCacheStats() const { return wordbook_cache_.stats(); }
    const PlaceReadTiming& readTiming() const { return timing_; }
private:
    PlaceState open();
    void releaseReaders();
    PlaceState reject(PlaceState state);
    CodeCacheBuffer& pixels_;
    SelectedDataset& selected_;
    uint8_t drive_=0;
    PlaceReadTiming timing_{}; // Must outlive the Readers that refer to its I/O counters.
    std::shared_ptr<ObservedFileReaderFactory> map_factory_, index_factory_, book_factory_;
    std::optional<CachedMapReader> map_;
    std::optional<DiskIndexedWordBookReader> wordbook_;
    WordBookCache wordbook_cache_;
    StorageObserver* observer_ = nullptr;
};

}  // namespace geoputer

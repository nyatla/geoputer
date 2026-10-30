#pragma once

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
class ReadFileSlots;

// Owned and called exclusively by StorageTask. No file handles cross task queues.
class PlaceLookup {
public:
    // root is a borrowed filesystem fixture prefix; production uses DatasetRoot.
    PlaceLookup(CodeCacheBuffer& pixels,SelectedDataset& selected,uint8_t number=0,std::string_view root=config::DatasetRoot)
        :pixels_(pixels),selected_(selected),root_(root) {
        selected_.number=number;
        selected_.info.state=!number?PlaceState::Unselected:number<=99?PlaceState::Waiting:PlaceState::MetadataError;
    }
    PlaceLookup(const PlaceLookup&) = delete;
    PlaceLookup& operator=(const PlaceLookup&) = delete;
    void setFiles(ReadFileSlots* files) {files_=files;} // Stable Storage owner; never a screen/job member.
    void setObserver(StorageObserver* observer) { observer_ = observer; }
    void searchInto(const PlaceRequest& request,PlaceResult& result,BorrowedReadBuffer* work,WordBookWorkspace* scratch);
    void observeAlarmInto(const PlaceRequest&,PlaceResult&,const alarm::Settings&,BorrowedReadBuffer*);
    void configure(uint8_t number,std::string_view language = ""); // Zero disables lookup without file I/O.
    void setDrive(uint8_t drive) { drive_=drive; }
    std::string_view selectedLanguage() const { return selected_.language; }
    void prepareInto(PlaceResult& result,BorrowedReadBuffer* work);
    void close(); // Explicit revalidation boundary; clears a latched failure.
    DatasetInfo datasetInfo() const { return selected_.info; }
    MapCacheStats cacheStats() const { return map_ ? map_->stats() : MapCacheStats{}; }
    WordBookCacheStats wordBookCacheStats() const { return wordbook_cache_.stats(); }
    const PlaceReadTiming& readTiming() const { return timing_; }
private:
    [[gnu::noinline]] PlaceState readSelection();
    PlaceState open(BorrowedReadBuffer* work);
    void releaseReaders();
    PlaceState reject(PlaceState state);
    CodeCacheBuffer& pixels_;
    SelectedDataset& selected_;
    std::string_view root_;
    uint8_t drive_=0;
    PlaceReadTiming timing_{}; // Must outlive the Readers that refer to its I/O counters.
    std::optional<FileReadSource> map_source_,index_source_,book_source_;
    std::optional<CachedMapReader> map_;
    std::optional<DiskIndexedWordBookReader> wordbook_;
    WordBookCache wordbook_cache_;
    StorageObserver* observer_ = nullptr;
    ReadFileSlots* files_=nullptr;
};

}  // namespace geoputer

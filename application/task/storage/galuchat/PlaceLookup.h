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

// Owned and called exclusively by GpsStorageTask. No file handles cross task queues.
class PlaceLookup {
public:
    // root is a borrowed filesystem fixture prefix; production uses DatasetRoot.
    PlaceLookup(CodeCacheBuffer& pixels,SelectedDataset& selected,std::string_view id={},std::string_view root=config::DatasetRoot)
        :pixels_(pixels),selected_(selected),root_(root) {
        const bool valid=selected_.id.assign(id);
        selected_.info.state=id.empty()?PlaceState::Unselected:valid?PlaceState::Waiting:PlaceState::MetadataError;
    }
    PlaceLookup(const PlaceLookup&) = delete;
    PlaceLookup& operator=(const PlaceLookup&) = delete;
    void setFiles(ReadFileSlots* files) {files_=files;} // Stable Storage owner; never a screen/job member.
    void setObserver(StorageObserver* observer) { observer_ = observer; }
    void searchInto(const PlaceRequest& request,PlaceResult& result,BorrowedReadBuffer* work,WordBookWorkspace* scratch);
    void observeAlarmInto(const PlaceRequest&,PlaceResult&,const alarm::Settings&,BorrowedReadBuffer*);
    void configure(std::string_view id,std::string_view language = ""); // Empty ID disables lookup.
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
    // One deterministic cell observation; the full-resolution plane remains shared.
    alarm::Observation alarm_observation_{};
    int alarm_cell_x_=0,alarm_cell_y_=0;
    alarm::Judgement alarm_judgement_=alarm::Judgement::Standard;
    bool alarm_cell_valid_=false;
};

}  // namespace geoputer

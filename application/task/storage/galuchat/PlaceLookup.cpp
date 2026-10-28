#include "PlaceLookup.h"
#include "TextEncoding.h"
#include "ObservedFileReader.h"
#include "../dataset/DatasetMetadata.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace geoputer {

void PlaceLookup::configure(std::string directory, std::string language) {
    close();
    selected_.directory = std::move(directory); selected_.language = selected_.directory.empty() ? "" : std::move(language);
    selected_.info.state=selected_.directory.empty()?PlaceState::Unselected:PlaceState::Waiting;
}
void PlaceLookup::releaseReaders() {
    const bool had_reader = wordbook_ || map_;
    wordbook_cache_.clear();
    wordbook_.reset(); map_.reset();map_factory_.reset();index_factory_.reset();book_factory_.reset();
    selected_.clearPrepared();
    if (had_reader && observer_) observer_->cacheState(RgCacheState::Unprepared, true);
}

void PlaceLookup::close() {
    releaseReaders();
    auto generation = selected_.info.generation + 1;
    if (!generation) ++generation;
    selected_.info = {selected_.directory.empty() ? PlaceState::Unselected : PlaceState::Waiting, 0, config::MaxDatasetChunks, generation};
}

PlaceState PlaceLookup::reject(PlaceState state) {
    releaseReaders();
    selected_.info.state = state;
    return state;
}

PlaceState PlaceLookup::open() {
    if (map_ && wordbook_) return PlaceState::Ready;
    // A rejected dataset stays rejected until an explicit close/re-open (e.g.
    // SD remount). GPS samples must not repeatedly scan an unusable file.
    if (datasetInactive(selected_.info.state)) return selected_.info.state;
    selected_.info.limit = config::MaxDatasetChunks;
    {
        const auto metadata = readDatasetMetadata(selected_.directory, UiLanguage::Japanese, selected_.language.c_str(), observer_);
        if (metadata.error != DatasetError::None) {
            switch (metadata.error) {
                case DatasetError::Missing: return PlaceState::DatasetMissing;
                case DatasetError::Version: return PlaceState::VersionError;
                case DatasetError::Data: return PlaceState::DataError;
                default: return PlaceState::MetadataError;
            }
        }
        selected_.language = metadata.language;
        selected_.display = metadata.display;
        selected_.map_path = selected_.directory + "/" + metadata.map;
        selected_.wordbook_path = selected_.directory + "/" + metadata.wordbook;
        SdAccessScope access(observer_);
        struct stat status{};
        if (stat(selected_.map_path.c_str(), &status) || !S_ISREG(status.st_mode) ||
            stat(selected_.wordbook_path.c_str(), &status) || !S_ISREG(status.st_mode)) return PlaceState::DatasetMissing;
    }
    // Preserve the SDK ReaderFactory/Reader API. Readers use the existing
    // mounted FatFs drive directly on ESP32; do not mount SD twice.
    std::optional<CachedMapReader> map;
    std::shared_ptr<ObservedFileReaderFactory> map_factory, index_factory;
    try {
        map_factory = std::make_shared<ObservedFileReaderFactory>(
            std::cref(selected_.map_path), observer_, &timing_.map_io,
            FileReadOptions{config::MapInitReadBufferBytes, config::FileReadThroughSkipBytes}, drive_);
        selected_.index_path=selected_.directory+"/_mapindex.dat";
        selected_.map_index=DatasetIndex(drive_,observer_).loadMap(selected_.index_path,selected_.map_path,&timing_.map_io,&timing_.index_io,&selected_.book_index,selected_.language);
        index_factory=std::make_shared<ObservedFileReaderFactory>(std::cref(selected_.index_path),observer_,&timing_.index_io,
            FileReadOptions{config::IndexReadBufferBytes,0}, drive_);
        map.emplace(pixels_,selected_.map_index,map_factory,index_factory,observer_);
        map_factory->setReadOptions({config::MapRectReadBufferBytes, config::FileReadThroughSkipBytes});
    } catch (const MapChunkLimitExceeded& error) {
        selected_.info.chunks = error.count;
        return selected_.info.state = PlaceState::TooManyChunks;
    }
    selected_.info.chunks = map->chunkCount();
    selected_.info.unit_inv_x = map->unitInvX(); selected_.info.unit_inv_y = map->unitInvY();
    const auto book_metadata=DatasetIndex(drive_,observer_).checkWordBook(selected_.book_index,selected_.wordbook_path,&timing_.wordbook_io);
    if (!wordBookUsesUtf8(book_metadata)) return PlaceState::EncodingError;
    auto book_factory = std::make_shared<ObservedFileReaderFactory>(std::cref(selected_.wordbook_path), observer_, &timing_.wordbook_io,
        FileReadOptions{config::WordBookReadBufferBytes, config::FileReadThroughSkipBytes}, drive_);
    DiskIndexedWordBookReader book(selected_.book_index, book_factory, index_factory);
    map_.emplace(std::move(*map)); wordbook_.emplace(std::move(book));map_factory_=std::move(map_factory);index_factory_=std::move(index_factory);book_factory_=std::move(book_factory);
    selected_.info.state = PlaceState::Ready;
    return PlaceState::Ready;
}

PlaceResult PlaceLookup::prepare() {
    PlaceResult result{};
    try { result.state = open(); }
    catch (const std::bad_alloc&) { result.state = PlaceState::OutOfMemory; }
    catch (const std::exception& error) {
        result.state = PlaceState::DataError;
        std::snprintf(result.detail, sizeof(result.detail), "%s", error.what());
    }
    if (datasetUnavailable(result.state)) reject(result.state);
    result.dataset = selected_.info;
    if (result.state == PlaceState::TooManyChunks)
        std::snprintf(result.detail, sizeof(result.detail), "Chunks: %llu / Max: %lu",
                      static_cast<unsigned long long>(selected_.info.chunks), static_cast<unsigned long>(selected_.info.limit));
    return result;
}

PlaceResult PlaceLookup::search(const PlaceRequest& request) {
    PlaceResult result;searchInto(request,result);return result;
}
void PlaceLookup::searchInto(const PlaceRequest& request,PlaceResult& result,BorrowedReadBuffer* work) {
    if constexpr (config::DetailedDiagnostics) timing_ = {};
    ElapsedTime total(timing_.total_us);
    result={};
    result.request = request;
    result.dataset = selected_.info;
    if (datasetInactive(selected_.info.state)) { result.state = selected_.info.state; return; }
    if (!request.fix.valid || !std::isfinite(request.fix.latitude) || !std::isfinite(request.fix.longitude) ||
        std::abs(request.fix.latitude) > 90 || std::abs(request.fix.longitude) > 180) {
        result.state = PlaceState::NoFix;
        return;
    }
    bool formatting = false;
    try {
        {
            ElapsedTime elapsed(timing_.open_us);
            result.state = open();
        }
        result.dataset = selected_.info;
        if (result.state != PlaceState::Ready) { reject(result.state); result.dataset = selected_.info; return; }
        struct ReaderBorrow {
            ObservedFileReaderFactory& factory;
            ReaderBorrow(ObservedFileReaderFactory& f,BorrowedReadBuffer* work):factory(f) {factory.setBorrowedBuffer(work);}
            ~ReaderBorrow() {factory.setBorrowedBuffer(nullptr);}
        } index_borrow(*index_factory_,work), map_borrow(*map_factory_,work), book_borrow(*book_factory_,work);
        // GLC decoding ends before Wordbook lookup. Each Wordbook phase also
        // closes its index Reader before opening the body, sharing one buffer.
        uint32_t code;
        const auto misses = map_->stats().misses;
        try {
            ElapsedTime elapsed(timing_.map_us);
            code = map_->readWgsPoint(request.fix.longitude, request.fix.latitude);
        } catch (...) {
            if (map_->stats().misses != misses) timing_.rect_us = map_->stats().last_rect_us;
            throw;
        }
        if (map_->stats().misses != misses) timing_.rect_us = map_->stats().last_rect_us;
        if (code == 0) {
            result.state = PlaceState::NotFound;
            return;
        }
        std::vector<std::string> components;
        {
            ElapsedTime elapsed(timing_.wordbook_us);
            if (wordbook_cache_.find(code, result.lines)) {
                result.region_code = code;
                return;
            }
            if (!wordbook_->readStringSetByCode(code, components)) throw std::runtime_error("invalid region code");
        }
        result.region_code = code;
        formatting = true;
        {
            ElapsedTime elapsed(timing_.text_us);
            formatPlaceLines(components, result.lines, selected_.display);
            wordbook_cache_.remember(static_cast<uint32_t>(code), result.lines);
        }
    } catch (const std::bad_alloc&) {
        result.state = PlaceState::OutOfMemory;
    } catch (const std::exception& error) {
        result.state = formatting ? PlaceState::EncodingError : PlaceState::DataError;
        std::snprintf(result.detail, sizeof(result.detail), "%s", error.what());
    }
    if (datasetUnavailable(result.state)) {
        reject(result.state);
        result.region_code = 0;
        std::memset(result.lines, 0, sizeof(result.lines));
    }
    result.dataset = selected_.info;
    return;
}

}  // namespace geoputer

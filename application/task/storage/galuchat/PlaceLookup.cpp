#include "PlaceLookup.h"
#include "TextEncoding.h"
#include "ObservedFileReader.h"
#include "../dataset/DatasetMetadata.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace geoputer {

namespace {
bool endsWith(const std::string& name, const char* suffix) {
    const std::string end(suffix);
    if (name.size() < end.size()) return false;
    for (size_t i = 0; i < end.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(name[name.size() - end.size() + i])) != end[i]) return false;
    return true;
}

}  // namespace

void PlaceLookup::configure(std::string directory, std::string language) {
    directory_ = std::move(directory); language_ = directory_.empty() ? "" : std::move(language);
    close(); manifest_ = true;
}
void PlaceLookup::releaseReaders() {
    const bool had_reader = wordbook_ || map_;
    wordbook_cache_.clear();
    display_ = {};
    wordbook_.reset(); map_.reset();
    if (had_reader && observer_) observer_->cacheState(RgCacheState::Unprepared, true);
}

void PlaceLookup::close() {
    releaseReaders();
    auto generation = dataset_.generation + 1;
    if (!generation) ++generation;
    dataset_ = {directory_.empty() ? PlaceState::Unselected : PlaceState::Waiting, 0, config::MaxDatasetChunks, generation};
}

PlaceState PlaceLookup::reject(PlaceState state) {
    releaseReaders();
    dataset_.state = state;
    return state;
}

PlaceState PlaceLookup::open() {
    if (map_ && wordbook_) return PlaceState::Ready;
    // A rejected dataset stays rejected until an explicit close/re-open (e.g.
    // SD remount). GPS samples must not repeatedly scan an unusable file.
    if (datasetInactive(dataset_.state)) return dataset_.state;
    dataset_.limit = config::MaxDatasetChunks;
    std::string mapPath, wordbookPath;
    unsigned maps = 0, books = 0;
    if (manifest_) {
        const auto metadata = readDatasetMetadata(directory_, UiLanguage::Japanese, language_.c_str(), observer_);
        if (metadata.error != DatasetError::None) {
            switch (metadata.error) {
                case DatasetError::Missing: return PlaceState::DatasetMissing;
                case DatasetError::Version: return PlaceState::VersionError;
                case DatasetError::Data: return PlaceState::DataError;
                default: return PlaceState::MetadataError;
            }
        }
        language_ = metadata.language;
        display_ = metadata.display;
        mapPath = directory_ + "/" + metadata.map;
        wordbookPath = directory_ + "/" + metadata.wordbook;
        SdAccessScope access(observer_);
        struct stat status{};
        if (stat(mapPath.c_str(), &status) || !S_ISREG(status.st_mode) ||
            stat(wordbookPath.c_str(), &status) || !S_ISREG(status.st_mode)) return PlaceState::DatasetMissing;
    } else {
    {
    SdAccessScope directory_access(observer_);
    std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(directory_.c_str()), closedir);
    if (!dir) return PlaceState::DatasetMissing;
    // Discovery paths survive the short directory I/O scope.

    while (const auto* entry = readdir(dir.get())) {
        const std::string name(entry->d_name);
        if (name.empty() || name[0] == '.') continue;
        const auto path = directory_ + "/" + name;
        struct stat status{};
        if (stat(path.c_str(), &status) || !S_ISREG(status.st_mode)) continue;
        if (endsWith(name, ".glc")) { ++maps; mapPath = path; }
        if (endsWith(name, ".giswordbook")) { ++books; wordbookPath = path; }
    }
    if (maps > 1 || books > 1) return PlaceState::AmbiguousFiles;
    if (!maps || !books) return PlaceState::DatasetMissing;
    }
    }
    // Preserve the SDK ReaderFactory/Reader API. Readers use the existing
    // mounted FatFs drive directly on ESP32; do not mount SD twice.
    std::unique_ptr<CachedMapReader> map;
    DatasetWordBookIndex book_info;
    std::shared_ptr<ObservedFileReaderFactory> index_factory;
    try {
        auto factory = std::make_shared<ObservedFileReaderFactory>(
            mapPath, observer_, &timing_.map_io,
            FileReadOptions{config::MapInitReadBufferBytes, config::FileReadThroughSkipBytes}, drive_);
        const auto index_path=directory_+"/_mapindex.dat";
        const auto info=DatasetIndex(drive_,observer_).loadMap(index_path,mapPath,&timing_.map_io,&timing_.index_io,&book_info,language_);
        index_factory=std::make_shared<ObservedFileReaderFactory>(index_path,observer_,&timing_.index_io,
            FileReadOptions{config::IndexReadBufferBytes,0}, drive_);
        map=std::make_unique<CachedMapReader>(info,factory,index_factory,observer_);
        factory->setReadOptions({config::MapRectReadBufferBytes, config::FileReadThroughSkipBytes});
    } catch (const MapChunkLimitExceeded& error) {
        dataset_.chunks = error.count;
        return dataset_.state = PlaceState::TooManyChunks;
    }
    dataset_.chunks = map->chunkCount();
    const auto book_metadata=DatasetIndex(drive_,observer_).checkWordBook(book_info,wordbookPath,&timing_.wordbook_io);
    if (!wordBookUsesUtf8(book_metadata)) return PlaceState::EncodingError;
    auto book = std::make_unique<DiskIndexedWordBookReader>(book_info,
        std::make_shared<ObservedFileReaderFactory>(wordbookPath, observer_, &timing_.wordbook_io,
            FileReadOptions{config::WordBookReadBufferBytes, config::FileReadThroughSkipBytes}, drive_), index_factory);
    map_ = std::move(map); wordbook_ = std::move(book);
    dataset_.state = PlaceState::Ready;
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
    result.dataset = dataset_;
    if (result.state == PlaceState::TooManyChunks)
        std::snprintf(result.detail, sizeof(result.detail), "Chunks: %llu / Max: %lu",
                      static_cast<unsigned long long>(dataset_.chunks), static_cast<unsigned long>(dataset_.limit));
    return result;
}

PlaceResult PlaceLookup::search(const PlaceRequest& request) {
    timing_ = {};
    ElapsedTime total(timing_.total_us);
    PlaceResult result{};
    result.request = request;
    result.dataset = dataset_;
    if (datasetInactive(dataset_.state)) { result.state = dataset_.state; return result; }
    if (!request.fix.valid || !std::isfinite(request.fix.latitude) || !std::isfinite(request.fix.longitude) ||
        std::abs(request.fix.latitude) > 90 || std::abs(request.fix.longitude) > 180) {
        result.state = PlaceState::NoFix;
        return result;
    }
    bool formatting = false;
    try {
        {
            ElapsedTime elapsed(timing_.open_us);
            result.state = open();
        }
        result.dataset = dataset_;
        if (result.state != PlaceState::Ready) { reject(result.state); result.dataset = dataset_; return result; }
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
            return result;
        }
        std::vector<std::string> components;
        {
            ElapsedTime elapsed(timing_.wordbook_us);
            if (wordbook_cache_.find(code, result.lines)) {
                result.region_code = code;
                return result;
            }
            if (!wordbook_->readStringSetByCode(code, components)) throw std::runtime_error("invalid region code");
        }
        result.region_code = code;
        formatting = true;
        {
            ElapsedTime elapsed(timing_.text_us);
            formatPlaceLines(components, result.lines, display_);
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
    result.dataset = dataset_;
    return result;
}

}  // namespace geoputer

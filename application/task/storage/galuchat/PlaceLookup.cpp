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

std::string metadataString(const std::string& metadata, const char* key) {
    const auto keyPos = metadata.find(std::string("\"") + key + "\"");
    if (keyPos == std::string::npos) return {};
    auto pos = metadata.find(':', keyPos);
    if (pos == std::string::npos) return {};
    do { ++pos; } while (pos < metadata.size() && std::isspace(static_cast<unsigned char>(metadata[pos])));
    if (pos >= metadata.size() || metadata[pos++] != '"') return {};
    const auto end = metadata.find('"', pos);
    return end == std::string::npos ? std::string{} : metadata.substr(pos, end - pos);
}
}  // namespace

void PlaceLookup::configure(std::string directory, std::string language) {
    close(); directory_ = std::move(directory); language_ = std::move(language); manifest_ = true;
}
bool PlaceLookup::describe(DatasetDetail& result) const {
    if (!map_ || !wordbook_) return false;
    const auto bounds = map_->coverage();
    result.west = bounds.x; result.south = bounds.y;
    result.east = bounds.x + bounds.width; result.north = bounds.y + bounds.height;
    result.unit_inv_x = map_->unitInvX(); result.unit_inv_y = map_->unitInvY();
    result.chunks = map_->chunkCount();
    return true;
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
    dataset_ = {PlaceState::Waiting, 0, config::MaxDatasetChunks, generation};
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
    if (datasetUnavailable(dataset_.state)) return dataset_.state;
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
    // Same ReaderFactory/Reader API as the SDK's file sample. ESP-IDF's VFS
    // exposes the already-mounted FatFs volume at /sd; do not mount SD twice.
    std::unique_ptr<CachedMapReader> map;
    try {
        auto factory = std::make_shared<ObservedFileReaderFactory>(
            mapPath, observer_, &timing_.map_io,
            FileReadOptions{config::MapInitReadBufferBytes, config::FileReadThroughSkipBytes});
        map = std::make_unique<CachedMapReader>(factory, observer_);
        // Index construction has released its Readers. Only future rectangle
        // Readers use this profile; no live buffer is resized.
        factory->setReadOptions({config::MapRectReadBufferBytes, config::FileReadThroughSkipBytes});
    } catch (const galuchat::MapCountLimitExceeded& error) {
        dataset_.chunks = error.count;
        return dataset_.state = PlaceState::TooManyChunks;
    }
    dataset_.chunks = map->chunkCount();
    auto book = std::make_unique<galuchat::GaluchatGisWordBookReader>(
        std::make_shared<ObservedFileReaderFactory>(wordbookPath, observer_, &timing_.wordbook_io,
            FileReadOptions{config::WordBookReadBufferBytes, config::FileReadThroughSkipBytes}), 0);
    auto encoding = metadataString(book->metadata(), "token_encoding");
    if (encoding.empty()) encoding = metadataString(book->metadata(), "text_encoding");
    for (auto& c : encoding) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (encoding != "utf8" && encoding != "utf-8") return PlaceState::EncodingError;
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
    if (datasetUnavailable(dataset_.state)) { result.state = dataset_.state; return result; }
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

#include "PlaceLookup.h"
#include "TextEncoding.h"
#include "PlaceLineOutput.h"
#include "ObservedFileReader.h"
#include "../dataset/DatasetMetadata.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace geoputer {

void PlaceLookup::configure(uint8_t number,std::string_view language) {
    close();
    selected_.number=number;
    const bool valid=selected_.setLanguage(number?language:std::string_view{});
    if(!valid)selected_.language[0]='\0';
    selected_.info.state=!number?PlaceState::Unselected:number<=99 && valid?PlaceState::Waiting:PlaceState::MetadataError;
}
void PlaceLookup::releaseReaders() {
    const bool had_reader = wordbook_ || map_;
    wordbook_cache_.clear();
    wordbook_.reset(); map_.reset();book_source_.reset();index_source_.reset();map_source_.reset();
    selected_.clearPrepared();
    if (had_reader && observer_) observer_->cacheState(RgCacheState::Unprepared, true);
}

void PlaceLookup::close() {
    releaseReaders();
    auto generation = selected_.info.generation + 1;
    if (!generation) ++generation;
    selected_.info = {!selected_.number ? PlaceState::Unselected : selected_.number<=99 ? PlaceState::Waiting : PlaceState::MetadataError, 0, config::MaxDatasetChunks, generation};
}

PlaceState PlaceLookup::reject(PlaceState state) {
    releaseReaders();
    selected_.info.state = state;
    return state;
}

PlaceState PlaceLookup::readSelection() {
    DatasetMetadata metadata;
    readDatasetMetadataInto(datasetDirectory(selected_.number,root_).view(),UiLanguage::Japanese,selected_.language,metadata,observer_);
    if (metadata.error != DatasetError::None) {
        switch (metadata.error) {
            case DatasetError::Missing: return PlaceState::DatasetMissing;
            case DatasetError::Version: return PlaceState::VersionError;
            case DatasetError::Data: return PlaceState::DataError;
            default: return PlaceState::MetadataError;
        }
    }
    if(!selected_.setLanguage(metadata.language))return PlaceState::MetadataError;
    selected_.display = metadata.display;
    if(!selected_.setNames(metadata.map,metadata.wordbook))return PlaceState::MetadataError;
    SdAccessScope access(observer_);
    struct stat status{};
    if (stat(selected_.file(DatasetFile::Map,root_).resolve().c_str(), &status) || !S_ISREG(status.st_mode) ||
        stat(selected_.file(DatasetFile::Wordbook,root_).resolve().c_str(), &status) || !S_ISREG(status.st_mode)) return PlaceState::DatasetMissing;
    return PlaceState::Ready;
}

PlaceState PlaceLookup::open(BorrowedReadBuffer* work) {
    if (map_ && wordbook_) return PlaceState::Ready;
    // A rejected dataset stays rejected until an explicit close/re-open (e.g.
    // SD remount). GPS samples must not repeatedly scan an unusable file.
    if (datasetInactive(selected_.info.state)) return selected_.info.state;
    selected_.info.limit = config::MaxDatasetChunks;
    const auto selection=readSelection();
    if(selection!=PlaceState::Ready)return selection;
    // Descriptions borrow stable selection paths. Consumers and every scoped
    // Reader are destroyed before these descriptions or paths are reset.
    try {
        map_source_.emplace(selected_.file(DatasetFile::Map,root_),observer_,&timing_.map_io,
            FileReadOptions{config::MapRectReadBufferBytes,config::FileReadThroughSkipBytes},drive_,files_?&files_->secondary():nullptr);
        selected_.map_index=DatasetIndex(drive_,observer_,nullptr,nullptr,files_).loadMap(selected_.file(DatasetFile::Index,root_).resolve().view(),selected_.file(DatasetFile::Map,root_).resolve().view(),&timing_.map_io,&timing_.index_io,&selected_.book_index,selected_.language,work);
        index_source_.emplace(selected_.file(DatasetFile::Index,root_),observer_,&timing_.index_io,
            FileReadOptions{config::IndexReadBufferBytes,0},drive_,files_?&files_->secondary():nullptr);
        map_.emplace(pixels_,selected_.map_index,*map_source_,*index_source_,observer_);
    } catch (const MapChunkLimitExceeded& error) {
        selected_.info.chunks=error.count;
        return selected_.info.state=PlaceState::TooManyChunks;
    }
    selected_.info.chunks=map_->chunkCount();
    selected_.info.unit_inv_x=map_->unitInvX();selected_.info.unit_inv_y=map_->unitInvY();
    DatasetIndex(drive_,observer_,nullptr,nullptr,files_).checkWordBook(selected_.book_index,selected_.file(DatasetFile::Wordbook,root_).resolve().view(),&timing_.wordbook_io,work);
    book_source_.emplace(selected_.file(DatasetFile::Wordbook,root_),observer_,&timing_.wordbook_io,
        FileReadOptions{config::WordBookReadBufferBytes,config::FileReadThroughSkipBytes},drive_,files_?&files_->secondary():nullptr);
    wordbook_.emplace(selected_.book_index,*book_source_,*index_source_);
    selected_.info.state = PlaceState::Ready;
    return PlaceState::Ready;
}

void PlaceLookup::prepareInto(PlaceResult& result,BorrowedReadBuffer* work) {
    result={};
    try { result.state = open(work); }
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
}

void PlaceLookup::searchInto(const PlaceRequest& request,PlaceResult& result,BorrowedReadBuffer* work,WordBookWorkspace* scratch) {
    if(!scratch || !work)throw std::invalid_argument("Place work memory is required");
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
    try {
        {
            ElapsedTime elapsed(timing_.open_us);
            result.state = open(work);
        }
        result.dataset = selected_.info;
        if (result.state != PlaceState::Ready) { reject(result.state); result.dataset = selected_.info; return; }
        // GLC decoding ends before Wordbook lookup. Each Wordbook phase also
        // closes its index Reader before opening the body, sharing one buffer.
        uint32_t code;
        const auto misses = map_->stats().misses;
        try {
            ElapsedTime elapsed(timing_.map_us);
            code = map_->readWgsPoint(request.fix.longitude, request.fix.latitude,work);
        } catch (...) {
            if (map_->stats().misses != misses) timing_.rect_us = map_->stats().last_rect_us;
            throw;
        }
        if (map_->stats().misses != misses) timing_.rect_us = map_->stats().last_rect_us;
        if (code == 0) {
            result.state = PlaceState::NotFound;
            return;
        }
        {
            ElapsedTime elapsed(timing_.wordbook_us);
            if (wordbook_cache_.find(code, result.lines)) {
                result.region_code = code;
                return;
            }
            if (!wordbook_->readPlaceLinesByCode(code,result.lines,selected_.display,*scratch,work))
                throw std::runtime_error("invalid region code");
        }
        result.region_code = code;
        wordbook_cache_.remember(static_cast<uint32_t>(code), result.lines);
    } catch (const std::bad_alloc&) {
        result.state = PlaceState::OutOfMemory;
    } catch (const PlaceEncodingError& error) {
        result.state=PlaceState::EncodingError;
        std::snprintf(result.detail,sizeof(result.detail),"%s",error.what());
    } catch (const std::exception& error) {
        result.state = PlaceState::DataError;
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

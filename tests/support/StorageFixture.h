#pragma once
#include "task/storage/dataset/DatasetIndex.h"
#include "task/storage/galuchat/PlaceLookup.h"
#include "task/storage/galuchat/MapSource.h"
#include <fstream>
#include <optional>
#include <vector>
#include <unistd.h>
namespace test_storage {
// Test callers explicitly own the same fixed resources that Storage owns on device.
struct Resources {
    geoputer::DatasetIndexWorkspace memory;
    geoputer::DatasetIndexWork work{memory};
    geoputer::ReadFileSlots files;
    geoputer::WordBookWorkspace words;
};
class Index:private Resources,public geoputer::DatasetIndex {
public:
    explicit Index(uint8_t drive=0,geoputer::StorageObserver* observer=nullptr,
        geoputer::IndexProgressCallback progress=nullptr,void* context=nullptr,
        geoputer::ReadFileSlots* files=nullptr,geoputer::DatasetIndexWork* work=nullptr)
        :geoputer::DatasetIndex(drive,observer,progress,context,files?files:&this->Resources::files,work?work:&this->Resources::work) {}
};
class Lookup:private Resources,public geoputer::PlaceLookup {
public:
    Lookup(geoputer::CodeCacheBuffer& pixels,geoputer::SelectedDataset& selected,uint8_t number=0,std::string_view root=geoputer::config::DatasetRoot)
        :geoputer::PlaceLookup(pixels,selected,number,root) {setFiles(&files);}
    geoputer::PlaceResult prepare() {geoputer::PlaceResult result;prepareInto(result,work.available());return result;}
    geoputer::PlaceResult search(const geoputer::PlaceRequest& request) {
        geoputer::PlaceResult result;searchInto(request,result,work.available(),&words);return result;
    }
};
inline geoputer::MapSourceResult boundary(const geoputer::SelectedDataset& selected,const geoputer::map::Request& request,
    uint8_t drive,geoputer::StorageObserver* observer,geoputer::MapSourceResult reusable,
    geoputer::BorrowedReadBuffer* supplied,geoputer::ReadFileSlot* slot,std::string_view root) {
    std::array<uint8_t,geoputer::config::MapRectReadBufferBytes> bytes;
    geoputer::BorrowedReadBuffer work(bytes.data(),bytes.size());geoputer::ReadFileSlot local;
    return geoputer::readBoundaryMap(selected,request,drive,observer,std::move(reusable),supplied?supplied:&work,slot?slot:&local,root);
}
class File {
    std::string native_,read_path_;
    std::vector<uint8_t> bytes_;
    geoputer::ReadFileSlot slot_;
    std::optional<geoputer::FileReadSource> source_;
    bool failed_=false;
    static std::string temporary() {char path[]="/tmp/rgp-sd-source-XXXXXX";const int fd=mkstemp(path);if(fd<0)throw std::runtime_error("fixture file");::close(fd);return path;}
public:
    geoputer::FileIoTiming timing{};
    uint32_t& creates=timing.opens;
    bool fail_reads=false;
    explicit File(const std::vector<uint8_t>& bytes,geoputer::FileReadOptions options={})
        :native_(temporary()),bytes_(bytes) {initialize(options);}
    explicit File(const std::string& path,geoputer::FileReadOptions options={})
        :native_(temporary()) {std::ifstream f(path,std::ios::binary);bytes_={std::istreambuf_iterator<char>(f),{}};initialize(options);}
    ~File() {source_.reset();std::remove(native_.c_str());}
    File(const File&)=delete;File& operator=(const File&)=delete;
    void replace(const std::vector<uint8_t>& bytes) {bytes_=bytes;write();}
    void sync() {if(failed_!=fail_reads){failed_=fail_reads;write();}}
    const geoputer::FileReadSource& source()const {return *source_;}
    operator geoputer::ScopedReaderSource()const {return geoputer::ScopedReaderSource(*source_);}
    const std::string& nativePath()const {return native_;}
private:
    void write() {std::ofstream f(native_,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(bytes_.data()),failed_?std::min<size_t>(12,bytes_.size()):bytes_.size());}
    void initialize(geoputer::FileReadOptions options) {
        write();
#ifdef GEOPUTER_TEST_FATFS
        read_path_="/sd"+native_;
#else
        read_path_=native_;
#endif
        source_.emplace(read_path_,nullptr,&timing,options,0,&slot_);
    }
};
class MapReader:public geoputer::DiskIndexedMapReader {
    std::array<uint8_t,geoputer::config::MapRectReadBufferBytes> bytes_;
    geoputer::BorrowedReadBuffer work_{bytes_.data(),bytes_.size()};
public:
    using geoputer::DiskIndexedMapReader::DiskIndexedMapReader;
    void readRect(const galuchat::Rect& rect,uint32_t* pixels)const {
        auto& work=const_cast<MapReader*>(this)->work_;
        geoputer::DiskIndexedMapReader::readRect(rect,pixels,&work);
    }
    void readRectDecimated(const galuchat::Rect& rect,unsigned scale,uint32_t* pixels)const {
        auto& work=const_cast<MapReader*>(this)->work_;
        geoputer::DiskIndexedMapReader::readRectDecimated(rect,scale,pixels,&work);
    }
};
} // namespace test_storage

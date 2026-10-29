#pragma once
// Synthetic cache tests supply a prevalidated snapshot, independently of the
// real-file CRC/source identity tests. This fixture alone owns its byte array.
#include <fstream>
#include <array>
#include <memory>
#include <vector>
#include "StorageFixture.h"
#include <galuchat/wgsmap3_reader.hpp>
#include "task/storage/galuchat/CachedMapReader.h"
namespace test_index {
using Bytes=std::vector<uint8_t>;
inline void put(uint8_t* p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i)); }
class OwnedCache {
    std::shared_ptr<test_storage::File> source_;
    test_storage::File index_;
    std::array<uint8_t,geoputer::config::MapRectReadBufferBytes> bytes_;
    geoputer::BorrowedReadBuffer work_{bytes_.data(),bytes_.size()};
    geoputer::CachedMapReader cache_;
public:
    OwnedCache(geoputer::CodeCacheBuffer& pixels,geoputer::DatasetMapIndex info,
        std::shared_ptr<test_storage::File> source,Bytes index,geoputer::StorageObserver* observer)
        :source_(std::move(source)),index_(index),cache_(pixels,info,source_->source(),index_.source(),observer) {}
    OwnedCache(const OwnedCache&)=delete;OwnedCache& operator=(const OwnedCache&)=delete;
    uint32_t readWgsPoint(double longitude,double latitude) {source_->sync();return cache_.readWgsPoint(longitude,latitude,&work_);}
    bool isCached(double longitude,double latitude)const {return cache_.isCached(longitude,latitude);}
    void invalidate() {cache_.invalidate();}
    const geoputer::MapCacheStats& stats()const {return cache_.stats();}
    size_t chunkCount()const {return cache_.chunkCount();}
    int unitInvX()const {return cache_.unitInvX();}int unitInvY()const {return cache_.unitInvY();}
};
inline OwnedCache cache(geoputer::CodeCacheBuffer& pixels,std::shared_ptr<test_storage::File> factory,const Bytes& bytes,geoputer::StorageObserver* observer=nullptr) {
    galuchat::BufferReader src(bytes);
    if(galuchat::detail::readAscii(src,4)!="GLCH")throw std::runtime_error("fixture GLCH");
    const auto payload=src.readMbUInt();const auto end=src.pos()+payload;
    src.skipInByte(16);const auto x=src.readMbUInt(),y=src.readMbUInt(),metadata=src.readMbUInt();src.skipInByte(metadata);
    const auto count=src.readMbUInt();
    if(count>geoputer::config::MaxDatasetChunks)throw geoputer::MapChunkLimitExceeded(count,geoputer::config::MaxDatasetChunks);
    std::vector<std::pair<int,int>> origins;
    for(size_t i=0;i<count;++i) {const auto ox=src.readMbInt(),oy=src.readMbInt();origins.emplace_back(ox,oy);}
    if(src.pos()!=end)throw std::runtime_error("fixture header");
    geoputer::DatasetMapIndex info;
    put(info.header.data()+16,64+count*40); put(info.header.data()+20,bytes.size());
    put(info.header.data()+36,count);put(info.header.data()+40,x);put(info.header.data()+44,y);put(info.header.data()+60,count*40);
    Bytes index(info.header.begin(),info.header.end());
    int west=INT32_MAX,south=INT32_MAX,east=INT32_MIN,north=INT32_MIN;
    for(size_t i=0;i<count;++i) {
        const auto start=src.pos();galuchat::GaluchatImageDataChunk01Reader image(src);const auto first=src.pos();image.skipToEnd();
        std::array<uint8_t,40> c{};
        put(c.data(),origins[i].first);put(c.data()+4,origins[i].second);put(c.data()+8,image.width);put(c.data()+12,image.height);put(c.data()+16,image.square_unit);
        const auto stored=src.pos()-start;
        put(c.data()+20,start);put(c.data()+24,stored);put(c.data()+28,first);put(c.data()+32,image.hus*image.vus);
        put(c.data()+36,stored>=geoputer::config::IndexMinChunkBytes?1:0);
        index.insert(index.end(),c.begin(),c.end());
        if(stored>=geoputer::config::IndexMinChunkBytes) {
            std::array<uint8_t,16> group{};put(group.data()+4,image.hus*image.vus);put(group.data()+8,first);put(group.data()+12,src.pos()-first);
            index.insert(index.end(),group.begin(),group.end());
        }
        west=std::min(west,origins[i].first);south=std::min(south,origins[i].second);
        east=std::max(east,origins[i].first+image.width);north=std::max(north,origins[i].second+image.height);
    }
    put(info.header.data()+16,index.size());put(info.header.data()+60,index.size()-64);
    std::copy(info.header.begin(),info.header.end(),index.begin());
    info.west=west;info.south=south;info.width=east-west;info.height=north-south;
    return OwnedCache(pixels,info,std::move(factory),std::move(index),observer);
}
inline OwnedCache cacheFile(geoputer::CodeCacheBuffer& pixels,std::shared_ptr<test_storage::File> source,const std::string& path) {
    std::ifstream f(path,std::ios::binary);return cache(pixels,std::move(source),Bytes(std::istreambuf_iterator<char>(f),{}));
}
inline void generate(const std::string& directory,const std::string& map,const std::string& book) {
    geoputer::DatasetMetadata metadata;geoputer::datasetCopyText(metadata.map,sizeof(metadata.map),map);geoputer::datasetCopyText(metadata.wordbook,sizeof(metadata.wordbook),book);geoputer::datasetCopyText(metadata.language,sizeof(metadata.language),"ja");
    const auto result=test_storage::Index{}.apply(directory,metadata,[](void*){return true;},nullptr);
    if(result!=geoputer::DatasetError::None)throw std::runtime_error("test index generation failed");
}
}

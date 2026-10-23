#pragma once
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include "task/storage/galuchat/CachedMapReader.h"
#include "task/storage/galuchat/DiskIndexedWordBookReader.h"
#include "task/storage/galuchat/ObservedFileReader.h"
#include "task/storage/galuchat/TextEncoding.h"
#include "task/storage/galuchat/WordBookCache.h"

namespace geoputer::benchmark {
struct WordBookSample {
    uint32_t code=0;
    char lines[2][64]{};
    std::vector<std::string> components;
    uint64_t total_us=0,map_us=0,book_us=0,text_us=0;
};
template<class Book>
WordBookSample queryWordBook(CachedMapReader& map,Book& book,WordBookCache& cache,
                             double lon,double lat,const PlaceDisplayFormat& format) {
    WordBookSample result;
    const auto start=monotonicMicros();
    auto stage=monotonicMicros();result.code=map.readWgsPoint(lon,lat);result.map_us=monotonicMicros()-stage;
    if(result.code) {
        stage=monotonicMicros();
        const bool hit=cache.find(result.code,result.lines);
        if(!hit && !book.readStringSetByCode(result.code,result.components))throw std::runtime_error("benchmark region missing");
        result.book_us=monotonicMicros()-stage;
        if(!hit) {
            stage=monotonicMicros();formatPlaceLines(result.components,result.lines,format);
            cache.remember(result.code,result.lines);result.text_us=monotonicMicros()-stage;
        }
    }
    result.total_us=monotonicMicros()-start;
    return result;
}
inline uint64_t wordBookHash(const std::vector<std::string>& components) {
    uint64_t hash=14695981039346656037ull;
    for(const auto& s:components) {
        for(unsigned char c:s) {hash^=c;hash*=1099511628211ull;}
        hash^=0;hash*=1099511628211ull;
    }
    return hash;
}
template<class Print>
void runWordBookIndexBenchmark(const std::string& directory,const DatasetMetadata& metadata,uint8_t drive,Print print) {
    const auto map_path=directory+"/"+metadata.map,book_path=directory+"/"+metadata.wordbook,index_path=directory+"/_mapindex.dat";
    DatasetIndex loader(drive);DatasetWordBookIndex book_info;
    const auto map_info=loader.loadMap(index_path,map_path,nullptr,nullptr,&book_info,metadata.language);
    if(!wordBookUsesUtf8(loader.checkWordBook(book_info,book_path)))throw std::runtime_error("benchmark needs UTF-8 Wordbook");
    FileIoTiming map_io{},book_io{},map_index_io{},book_index_io{};
    auto map_source=std::make_shared<ObservedFileReaderFactory>(map_path,nullptr,&map_io,FileReadOptions{16384,4096});
    auto book_source=std::make_shared<ObservedFileReaderFactory>(book_path,nullptr,&book_io,FileReadOptions{4096,4096});
    auto map_index=std::make_shared<ObservedFileReaderFactory>(index_path,nullptr,&map_index_io,FileReadOptions{4096,0});
    auto book_index=std::make_shared<ObservedFileReaderFactory>(index_path,nullptr,&book_index_io,FileReadOptions{4096,0});
    auto map=std::make_unique<CachedMapReader>(map_info,map_source,map_index);
    auto sequential=std::make_unique<galuchat::GaluchatGisWordBookReader>(book_source,0);
    auto indexed=std::make_unique<DiskIndexedWordBookReader>(book_info,book_source,book_index);
    auto names=std::make_unique<WordBookCache>();
    // Allocate the common image before timing either Wordbook path.
    (void)map->readWgsPoint(140.027194,35.681641);
    std::array<uint64_t,3> hashes{};std::array<bool,3> have{};
    print("[WB-BEGIN] path=%s map_buffer=16384 book_buffer=4096 index_buffer=4096 observer=none\n",book_path.c_str());
    const double centers[3][2]={{140.027194,35.681641},{141.4632,24.2349},{0,0}};
    for(unsigned round=0;round<3;++round)for(unsigned order=0;order<2;++order) {
        const bool saved=bool(order^(round%2));const char* mode=saved?"indexed":"sequential";
        auto run=[&](auto& reader) {
            for(unsigned scenario=0;scenario<3;++scenario) {
                map->invalidate();names->clear();map_io={};book_io={};map_index_io={};book_index_io={};
                const auto result=queryWordBook(*map,reader,*names,centers[scenario][0],centers[scenario][1],metadata.display);
                const auto m=map_io,b=book_io,mi=map_index_io,bi=book_index_io;
                const auto hash=wordBookHash(result.components);
                if(have[scenario] && hash!=hashes[scenario])throw std::runtime_error("sequential/indexed Wordbook mismatch");
                have[scenario]=true;hashes[scenario]=hash;
                map_io={};book_io={};map_index_io={};book_index_io={};
                const auto hit=queryWordBook(*map,reader,*names,centers[scenario][0],centers[scenario][1],metadata.display);
                if(hit.code!=result.code || std::memcmp(hit.lines,result.lines,sizeof(hit.lines)))throw std::runtime_error("cached place changed");
                if(map_io.opens || map_io.bytes || book_io.opens || book_io.bytes || map_index_io.opens || map_index_io.bytes || book_index_io.opens || book_index_io.bytes)
                    throw std::runtime_error("cache hit performed file IO");
                print("[WB-CASE] round=%u mode=%s scenario=%u code=%u total_us=%llu map_us=%llu book_us=%llu text_us=%llu hit_us=%llu hash=%llu\n",
                    round+1,mode,scenario,result.code,static_cast<unsigned long long>(result.total_us),
                    static_cast<unsigned long long>(result.map_us),static_cast<unsigned long long>(result.book_us),
                    static_cast<unsigned long long>(result.text_us),static_cast<unsigned long long>(hit.total_us),static_cast<unsigned long long>(hash));
                auto io=[&](const char* file,const FileIoTiming& v) {
                    print("[WB-IO] round=%u mode=%s scenario=%u file=%s bytes=%llu reads=%u seeks=%u opens=%u closes=%u read_us=%llu seek_us=%llu open_us=%llu close_us=%llu\n",
                        round+1,mode,scenario,file,static_cast<unsigned long long>(v.bytes),v.reads,v.seeks,v.opens,v.closes,
                        static_cast<unsigned long long>(v.read_us),static_cast<unsigned long long>(v.seek_us),
                        static_cast<unsigned long long>(v.open_us),static_cast<unsigned long long>(v.close_us));
                };
                io("map",m);io("book",b);io("map_index",mi);io("book_index",bi);
            }
        };
        if(saved)run(*indexed);else run(*sequential);
    }
    print("[WB-DONE] equal=1 cases=18\n");
}
} // namespace geoputer::benchmark

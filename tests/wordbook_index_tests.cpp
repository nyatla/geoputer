#include "support/PlaceFormatOracle.h"
#include <galuchat/std_file_reader.hpp>
#include "support/StorageFixture.h"
#include "PlaceText.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "task/storage/galuchat/DiskIndexedWordBookReader.h"
#include "task/storage/galuchat/ObservedFileReader.h"

namespace {
namespace fs=std::filesystem;
void check(bool ok,const char* why) { if(!ok)throw std::runtime_error(why); }
template<class F> void fails(F call,const char* why) {
    bool failed=false;try {call();}catch(const std::exception&) {failed=true;}check(failed,why);
}
uint32_t get(const uint8_t* p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void put(uint8_t* p,uint32_t v) {for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
std::vector<uint8_t> bytes(const fs::path& p) {std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
}
void testWordBookIndex(const std::string& dir,const std::string& map,const std::string& book) {
    using namespace geoputer;
    test_storage::Index loader;
    const auto path=dir+"/_mapindex.dat";
    DatasetWordBookIndex info;
    loader.loadMap(path,map,nullptr,nullptr,&info,"ja");
    loader.checkWordBook(info,book);
    FileIoTiming source_io{},index_io{};
    ReadFileSlot slot;std::array<uint8_t,4096> memory;BorrowedReadBuffer work(memory.data(),memory.size());
    WordBookWorkspace scratch;char fixed[2][config::PlaceResultLineBytes];
    FileReadSource source(book,nullptr,&source_io,{},0,&slot),index(path,nullptr,&index_io,{4096,0},0,&slot);
    DiskIndexedWordBookReader reader(info,source,index);
    auto legacy_source=std::make_shared<galuchat::FileReaderFactory>(book);
    galuchat::GaluchatGisWordBookReader legacy(legacy_source,0);
    // The SDK is an independent format oracle; the app only uses fixed output.
    auto query=[&](const DiskIndexedWordBookReader& input,int64_t code,std::vector<std::string>& rows) {
        const bool found=input.readPlaceLinesByCode(code,fixed,{},scratch,&work);
        rows.clear();if(found)for(auto& line:fixed) {char short_line[config::PlaceCachedLineBytes];PlaceLineBuilder b(PlaceTextMode::Japanese16,placeLogicalLinePixels());b.append(line);b.finish(short_line);rows.emplace_back(short_line);}
        check(!work.inUse() && !slot.inUse(),"query retained caller resources");return found;
    };
    auto expectedRows=[&](size_t code) {char rows[2][config::PlaceCachedLineBytes];formatPlaceLines(legacy.readStringSet(code),rows,{},PlaceTextMode::Japanese16,placeLogicalLinePixels());return std::vector<std::string>{rows[0],rows[1]};};
    const auto count=legacy.recordCount();
    std::vector<size_t> points{0,count-1,count/4,count/2,count*3/4};
    if(count>66081)points.push_back(66081);
    for(size_t i=0;i<std::min<size_t>(count,20);++i)points.push_back((i*7919)%count);
    for(auto point:points) {
        std::vector<std::string> actual;
        check(query(reader,point+1,actual),"indexed lookup false for a valid code");
        check(actual==expectedRows(point),"indexed Wordbook differs from sequential Reader");
    }
    source_io={};index_io={};
    std::vector<std::string> actual;
    const auto sample=count>66081?66082:count;
    check(query(reader,sample,actual) && actual==expectedRows(sample-1),"sample name mismatch");
    check(index_io.opens && index_io.opens==index_io.closes,"index must close between body phases");
    std::cout<<"Wordbook fixed output: "<<points.size()<<" names match SDK oracle; sample="<<sample<<" source_bytes="<<source_io.bytes<<" index_bytes="<<index_io.bytes<<'\n';
    source_io={};index_io={};actual={"old"};
    check(!query(reader,0,actual) && actual.empty() && !source_io.opens && !index_io.opens,"zero code must not access storage");
    fails([&]{query(reader,count+1,actual);},"out of range code accepted");
    check(!source_io.opens && !index_io.opens,"bad code should fail before opening files");
    DatasetWordBookIndex wrong;
    fails([&]{loader.loadMap(path,map,nullptr,nullptr,&wrong,"xx");},"missing selected language accepted");
    // A valid but changed timestamp cannot silently reuse the old index.
    const auto stamp=fs::last_write_time(book);
    fs::last_write_time(book,stamp+std::chrono::seconds(60));
    fails([&]{loader.checkWordBook(info,book);},"changed Wordbook timestamp accepted");
    fs::last_write_time(book,stamp);
    loader.checkWordBook(info,book); // Restoring the timestamp permits reuse.
    const auto alias=fs::path(dir)/"alias.giswordbook";
    fs::copy_file(book,alias,fs::copy_options::overwrite_existing);
    fails([&]{loader.checkWordBook(info,alias.string());},"wrong Wordbook basename accepted");
    fs::remove(alias);
    const auto original=bytes(book);auto changed=original;changed[8]^=1;
    {std::ofstream f(book,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(changed.data()),changed.size());}
    fs::last_write_time(book,stamp);
    fails([&]{loader.checkWordBook(info,book);},"changed GW00 with unchanged mtime/size accepted");
    {std::ofstream f(book,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(original.data()),original.size());}
    fs::last_write_time(book,stamp);
    loader.checkWordBook(info,book); // Restoring bytes permits reuse.
    // Runtime header/table mutations must fail without recomputing the full CRC.
    auto data=bytes(path);
    test_storage::File mutated(data);
    DiskIndexedWordBookReader broken(info,source,mutated);
    const auto good=data;
    data[info.section_offset+90]^=1;
    fails([&]{mutated.replace(data);query(broken,sample,actual);},"runtime header mutation accepted");
    data=good;put(data.data()+info.section_offset+128+4,0);
    fails([&]{mutated.replace(data);query(broken,sample,actual);},"zero-length root accepted");
    data=good;
    const auto tt=info.section_offset+128+get(info.header.data()+92)*16;
    put(data.data()+tt+4,0xffffffff);
    fails([&]{mutated.replace(data);query(broken,sample,actual);},"bad text page offset accepted");
    data=good;
    const auto tm=tt+get(info.header.data()+96)*8;
    put(data.data()+tm+8,0);
    fails([&]{mutated.replace(data);query(broken,sample,actual);},"zero token width accepted");
    // Empty and unsorted target handling belongs to the new explicit SDK API.
    std::vector<uint8_t> raw{65,66,67};galuchat::BufferReader raw_source(raw);
    check(galuchat::GaluchatGisWordBookReader::readTokensFromPage(raw_source,0,3,1,{}).empty(),"empty token selection");
    fails([&]{galuchat::GaluchatGisWordBookReader::readTokensFromPage(raw_source,0,3,1,{1,0});},"unsorted token selection");
    // Two zero-token strings: terminators still need to be decoded in order.
    std::vector<uint8_t> empty_page{0,0,2,4,1,0x82,0,0};
    galuchat::BufferReader empty_source(empty_page);
    const auto empty=galuchat::GaluchatGisWordBookReader::readTokenIdSetsFromPage(empty_source,empty_page.size(),0,2,1,{0,1});
    check(empty.size()==2 && empty[0].second.empty() && empty[1].second.empty(),"zero-token text records");
    for(const std::vector<uint8_t> malformed : {std::vector<uint8_t>{0,1,1,1}, {0,1,1,1,0}}) {
        galuchat::BufferReader input(malformed);
        fails([&]{galuchat::GaluchatGisWordBookReader::readTokenIdSetsFromPage(input,malformed.size(),0,1,1,{0});},"truncated/unterminated TT00 accepted");
    }
    galuchat::BufferReader root_source(raw);
    fails([&]{galuchat::GaluchatGisWordBookReader::readCodeSetFromRoot(root_source,{1,1,1,8,8,false},0);},"invalid root start bit accepted");
    galuchat::BufferReader tt_source(raw);
    check(galuchat::GaluchatGisWordBookReader::readTokenIdSetsFromPage(tt_source,3,0,1,1,{}).empty() && !tt_source.pos(),"empty text selection must not read packets");
}

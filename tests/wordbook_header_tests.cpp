#include "task/storage/dataset/DatasetIndex.h"
#include "task/storage/ReadFile.h"
#include <galuchat/gis_wordbook_reader.hpp>
#include <filesystem>
#include <array>
#include <algorithm>
#include <fstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdio>
namespace index_heap {extern bool track;extern size_t calls,bytes,four_k,largest;}
namespace {
using Bytes=std::vector<uint8_t>;
void check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
void mb(Bytes& out,uint64_t n) {
    if(n<252){out.push_back(n);return;}
    const uint64_t bases[]={252,508,66044,16843260};
    for(unsigned i=0;i<4;++i)if(n-bases[i]<(uint64_t{1}<<(8*(i+1)))) {
        out.push_back(255-i);n-=bases[i];for(unsigned j=i+1;j;--j)out.push_back(n>>(8*(j-1)));return;
    }
    throw std::runtime_error("fixture integer out of range");
}
Bytes numbers(std::initializer_list<uint64_t> fields) {Bytes out;for(auto n:fields)mb(out,n);return out;}
void chunk(Bytes& out,const char* name,const Bytes& body) {out.insert(out.end(),name,name+4);mb(out,body.size());out.insert(out.end(),body.begin(),body.end());}
Bytes book(const Bytes& gw,const Bytes& tm,const Bytes& tt,const Bytes& ti) {Bytes out;chunk(out,"GW00",gw);chunk(out,"TM00",tm);chunk(out,"TT00",tt);chunk(out,"TI00",ti);return out;}
}
void testWordBookHeaders(const std::filesystem::path& directory) {
    using namespace geoputer;namespace fs=std::filesystem;
    const auto path=(directory/"header-candidate.giswordbook").string();
    ReadFileSlots files;static DatasetIndexWorkspace memory;DatasetIndexWork work(memory);
    DatasetIndex reader(0,nullptr,nullptr,nullptr,&files,&work);
    Bytes gw(16,0);const std::string magic="GisWordBook/0";std::copy(magic.begin(),magic.end(),gw.begin());
    const auto empty_gw=gw;mb(gw,131072);gw.resize(gw.size()+131072,0xff); // Larger than the entire 96KiB reservation.
    auto tm=numbers({1,1});tm.push_back(0);
    auto tt=numbers({1,1,1,1});tt.push_back(0);
    auto ti=numbers({1,1,1,1,1});ti.push_back(0);
    auto inspect=[&](const Bytes& data,bool accepted) {
        {std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(data.data()),data.size());}
        // The SDK constructor is an independent oracle for standard-header acceptance.
        bool sdk_ok=true;try {galuchat::GaluchatGisWordBookReader legacy(data,0);}catch(const std::exception&){sdk_ok=false;}
        check(sdk_ok==accepted,"SDK/header fixture expectation");
        bool actual=true;index_heap::calls=index_heap::bytes=index_heap::four_k=index_heap::largest=0;index_heap::track=true;
        try {reader.inspectWordBook(path);}catch(const std::exception&){actual=false;}
        index_heap::track=false;
        check(actual==accepted && !work.inUse() && !files.inUse(),"SD header validation and resource return match SDK");
        if(accepted)check(!index_heap::calls,"opaque metadata and standard headers require zero C++ allocations");
    };
    inspect(book(gw,tm,tt,ti),true);
    auto empty=empty_gw;mb(empty,0);inspect(book(empty,tm,tt,ti),true);
    auto bad=empty;bad[0]='X';inspect(book(bad,tm,tt,ti),false);
    bad=empty_gw;mb(bad,1);inspect(book(bad,tm,tt,ti),false);
    inspect(book(empty,Bytes{1},tt,ti),false);
    inspect(book(empty,tm,Bytes{1,1,1},ti),false);
    for(auto bits:{0u,17u}) {auto changed=numbers({1,1,1,bits});changed.push_back(0);inspect(book(empty,tm,changed,ti),false);}
    for(auto fields:{std::array<uint32_t,5>{0,1,1,1,1},{1,0,1,1,1},{1,1,0,1,1},{1,1,32,1,1},{1,1,1,0,1},{1,1,1,1,0},{1,1,1,1,2}}) {
        auto changed=numbers({fields[0],fields[1],fields[2],fields[3],fields[4]});changed.push_back(0);
        inspect(book(empty,tm,tt,changed),false);
    }
    auto too_many_tokens=numbers({3,1});too_many_tokens.push_back(0);inspect(book(empty,too_many_tokens,tt,ti),false);
    auto too_many_components=numbers({3,1,1,1});too_many_components.push_back(0);inspect(book(empty,tm,too_many_components,ti),false);
    bad=book(empty,tm,tt,ti);bad.push_back(0);inspect(bad,false);
    bad=book(empty,tm,tt,ti);bad.pop_back();inspect(bad,false);
    bad=book(empty,tm,tt,ti);bad[0]='T';inspect(bad,false);
    // Recovery after every malformed header uses the same pool.
    inspect(book(gw,tm,tt,ti),true);fs::remove(path);
    std::puts("PASS: SD Wordbook headers match SDK checks; 128KiB opaque metadata has zero C++ allocations; malformed chunks/fields and recovery");
}

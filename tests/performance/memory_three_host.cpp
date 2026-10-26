#include "task/storage/dataset/DatasetIndex.h"
#include "task/storage/dataset/DatasetMetadata.h"
#include "task/storage/galuchat/DiskIndexedWordBookReader.h"
#include "task/storage/galuchat/ObservedFileReader.h"
#include "task/storage/log/LogReader.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <string_view>

using namespace geoputer;
namespace allocations {
struct alignas(std::max_align_t) Header {size_t bytes;unsigned epoch;};
struct Counts {size_t calls=0,bytes=0,live=0,peak=0;};
unsigned epoch=0;bool tracking=false;Counts counts;
}
void* operator new(size_t n) {
    using namespace allocations;
    auto* h=static_cast<Header*>(std::malloc(sizeof(Header)+(n?n:1)));
    if(!h)throw std::bad_alloc();
    h->bytes=n;h->epoch=tracking?epoch:0;
    if(tracking){++counts.calls;counts.bytes+=n;counts.live+=n;counts.peak=std::max(counts.peak,counts.live);}
    return h+1;
}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {
    if(!p)return;
    using namespace allocations;auto* h=static_cast<Header*>(p)-1;
    if(h->epoch && h->epoch==epoch)counts.live-=h->bytes;
    std::free(h);
}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,size_t) noexcept {::operator delete(p);}
void* operator new(size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {::operator delete(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {::operator delete(p);}

namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
uint64_t fingerprint;
void hash(std::string_view s) {
    fingerprint^=s.size();fingerprint*=1099511628211ull;
    for(unsigned char c:s){fingerprint^=c;fingerprint*=1099511628211ull;}
}
template<class F> void profile(const char* name,F work) {
    using namespace allocations;counts={};++epoch;tracking=true;fingerprint=14695981039346656037ull;
    const auto start=std::chrono::steady_clock::now();work();
    const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    tracking=false;
    std::printf("{\"case\":\"%s\",\"allocations\":%zu,\"allocated_bytes\":%zu,\"peak_bytes\":%zu,\"retained_bytes\":%zu,\"us\":%lld,\"fingerprint\":%llu}\n",
        name,counts.calls,counts.bytes,counts.peak,counts.live,static_cast<long long>(us),static_cast<unsigned long long>(fingerprint));
}
void io(const char* name,const FileIoTiming& t) {
    std::printf("{\"case\":\"%s\",\"reads\":%lu,\"bytes\":%llu,\"seeks\":%lu,\"opens\":%lu,\"closes\":%lu}\n",
        name,static_cast<unsigned long>(t.reads),static_cast<unsigned long long>(t.bytes),static_cast<unsigned long>(t.seeks),
        static_cast<unsigned long>(t.opens),static_cast<unsigned long>(t.closes));
}
}

int main(int argc,char** argv) {try {
    check(argc==4,"usage: work_directory map Wordbook");
    namespace fs=std::filesystem;const fs::path dir=argv[1];fs::create_directories(dir);
    fs::create_symlink(fs::absolute(argv[2]),dir/"map.glc");fs::create_symlink(fs::absolute(argv[3]),dir/"names.giswordbook");
    const std::string a(31,'a'),b=std::string(30,'a')+"b";
    auto write_metadata=[&](bool multiple) {
        std::ofstream ini(dir/"dataset.ini");ini<<"[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n";
        if(multiple)ini<<"["<<b<<"]\ntitle=Second\nwordbook=names.giswordbook\n";
        ini<<"[ja]\ntitle=Japan\nwordbook=names.giswordbook\n";
        if(multiple)ini<<"["<<a<<"]\ntitle=First\nwordbook=names.giswordbook\n";
    };
    const auto directory=dir.string();
    auto metadata=[&](const char* language) {
        for(unsigned i=0;i<20;++i){const auto m=readDatasetMetadata(directory,UiLanguage::Japanese,language);
            check(m.error==DatasetError::None,"metadata");hash(m.language);hash(m.title);hash(m.wordbook);for(const auto& id:m.languages)hash(id);}
    };
    write_metadata(false);profile("metadata_ja_only",[&]{metadata("ja");});
    write_metadata(true);profile("metadata_short_selected",[&]{metadata("ja");});
    profile("metadata_long_selected",[&]{metadata(a.c_str());});
    const auto m=readDatasetMetadata(directory,UiLanguage::Japanese);
    DatasetIndex index;check(index.rebuild(directory,m)==DatasetError::None,"build fixture index");
    const auto index_path=(dir/"_mapindex.dat").string();
    profile("index_validate_long_ids",[&]{for(unsigned i=0;i<20;++i){check(index.validate(index_path),"index validation");hash("valid");}});
    DatasetWordBookIndex info;index.loadMap(index_path,(dir/"map.glc").string(),nullptr,nullptr,&info,"ja");
    FileIoTiming book_io{},index_io{};
    auto book_factory=std::make_shared<ObservedFileReaderFactory>((dir/"names.giswordbook").string(),nullptr,&book_io);
    auto index_factory=std::make_shared<ObservedFileReaderFactory>(index_path,nullptr,&index_io,FileReadOptions{4096,0});
    DiskIndexedWordBookReader book(info,book_factory,index_factory);
    const auto count=index_format::get(info.header.data()+index_format::Wordbook::RecordCount);
    profile("wordbook_24_queries",[&]{for(unsigned i=0;i<24;++i){std::vector<std::string> result;
        check(book.readStringSetByCode(1+(uint64_t(i)*7919)%count,result),"lookup");for(const auto& part:result)hash(part);}});
    io("wordbook_io",book_io);io("wordbook_index_io",index_io);

    const auto log_dir=dir/"logs";fs::create_directory(log_dir);
    for(unsigned i=0;i<32;++i) {
        char name[48];std::snprintf(name,sizeof(name),"20261026_0100%02u_0.gps.dat",i);
        uint8_t header[LogHeaderBytes],record[LogRecordBytes];encodeLogHeader(header,100+i,UINT64_MAX);
        GpsFix fix{35.0+i*.00001,140.0,0,true};check(encodeLogRecord(fix,record),"encode record");
        std::ofstream f(log_dir/name,std::ios::binary);f.write(reinterpret_cast<char*>(header),sizeof(header));f.write(reinterpret_cast<char*>(record),sizeof(record));
    }
    LogReader logs(log_dir.string());LogReadRequest req{};req.kind=LogReadKind::List;req.token=1;
    auto list=[&]{check(logs.begin(req),"list begin");while(!logs.done())logs.step();check(logs.result().error==LogReadError::None,"list result");for(unsigned n=0;n<logs.result().count;++n)hash(logs.result().data.names[n]);};
    profile("log_list_cold",list);
    profile("log_list_warm_20",[&]{for(unsigned i=0;i<20;++i)list();});
    std::strcpy(req.name,logs.result().data.names[0]);req.kind=LogReadKind::Summary;
    profile("log_summary_warm_20",[&]{for(unsigned i=0;i<20;++i){check(logs.begin(req),"summary begin");while(!logs.done())logs.step();check(logs.result().error==LogReadError::None && logs.result().summary.records==1,"summary result");hash(req.name);}});
}catch(const std::exception& e){allocations::tracking=false;std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}

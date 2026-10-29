#include "support/StorageFixture.h"
#include <cstring>
#include "task/storage/dataset/DatasetIndex.h"
#include "task/storage/ReadFile.h"
#include <array>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <unistd.h>
#include <cstdlib>
#include <new>

namespace index_heap {
bool track=false;size_t four_k=0,largest=0,calls=0,bytes=0;
}
void* operator new(size_t bytes) {
    if(index_heap::track){++index_heap::calls;index_heap::bytes+=bytes;index_heap::four_k+=bytes==4096;index_heap::largest=std::max(index_heap::largest,bytes);}
    if(auto* p=std::malloc(bytes?bytes:1))return p;
    throw std::bad_alloc();
}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}

using namespace geoputer;
namespace fs=std::filesystem;
namespace {
void check(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
enum class Fault { None,Write,Sync,Close,Publish,Restore };
Fault fault=Fault::None;
std::vector<uint8_t> read(const fs::path& path) {
    std::ifstream file(path,std::ios::binary); return {std::istreambuf_iterator<char>(file),{}};
}
void write(const fs::path& path,const std::vector<uint8_t>& bytes) {
    std::ofstream file(path,std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); check(bool(file),"test fixture write");
}
struct Observer : StorageObserver {
    unsigned depth=0,accesses=0;
    fs::path change_source,temporary;
    bool changed=false;
    ReadFileSlots* files=nullptr;
    unsigned max_files=0;
    void sdBegin() override {
        ++depth; ++accesses;
        if(files)max_files=std::max(max_files,unsigned(files->primary().inUse())+unsigned(files->secondary().inUse()));
        if (!changed && !change_source.empty() && fs::exists(temporary) && fs::file_size(temporary)>100) {
            fs::last_write_time(change_source,fs::last_write_time(change_source)+std::chrono::seconds(60)); changed=true;
        }
    }
    void sdEnd() override { check(depth>0,"balanced SD end"); --depth; }
    void cacheState(RgCacheState,bool) override {}
};
struct Commit {
    test_storage::Index* index;
    std::string final;
    bool success=true;
    unsigned calls=0;
    static bool save(void* opaque) {
        auto& self=*static_cast<Commit*>(opaque); ++self.calls;
        check(self.index->validate(self.final),"published index must be readable and valid before NVS save");
        return self.success;
    }
};
}
extern "C" {
size_t __real_fwrite(const void*,size_t,size_t,FILE*);
size_t __wrap_fwrite(const void* data,size_t size,size_t count,FILE* file) {
    if (fault==Fault::Write) { fault=Fault::None; errno=EIO; return 0; }
    return __real_fwrite(data,size,count,file);
}
int __real_fsync(int);
int __wrap_fsync(int fd) {
    if (fault==Fault::Sync) { fault=Fault::None; errno=EIO; return -1; }
    return __real_fsync(fd);
}
int __real_fclose(FILE*);
int __wrap_fclose(FILE* file) {
    // Fault injection must not allocate in the normal close/heap-counting path.
    if(fault!=Fault::Close)return __real_fclose(file);
    std::array<char,4096> path{};
    const auto link="/proc/self/fd/"+std::to_string(fileno(file));
    const auto length=::readlink(link.c_str(),path.data(),path.size()-1);
    const bool staging=length>0 && std::string(path.data(),static_cast<size_t>(length)).find("_mapindex.tmp")!=std::string::npos;
    const auto result=__real_fclose(file);
    if (fault==Fault::Close && staging) { fault=Fault::None; errno=EIO; return EOF; }
    return result;
}
int __real_rename(const char*,const char*);
int __wrap_rename(const char* from,const char* to) {
    const std::string_view source(from);
    if ((fault==Fault::Publish && source.find("_mapindex.tmp")!=std::string_view::npos) ||
        (fault==Fault::Restore && source.find("_mapindex.bak")!=std::string_view::npos)) {
        fault=Fault::None; errno=EIO; return -1;
    }
    return __real_rename(from,to);
}
}
void testWordBookIndex(const std::string&,const std::string&,const std::string&);
void testWordBookHeaders(const fs::path&);
void testIndexMaintenance(const fs::path& parent,const fs::path& source_map,const fs::path& source_book) {
    const auto dir=parent/"maintenance";fs::create_directories(dir);
    fs::copy_file(source_map,dir/"map.glc");fs::copy_file(source_book,dir/"ja.giswordbook");fs::copy_file(source_book,dir/"en.giswordbook");
    {std::ofstream ini(dir/"dataset.ini");ini<<"[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=Japan\nwordbook=ja.giswordbook\n[en]\ntitle=Japan\nwordbook=en.giswordbook\n";}
    auto metadata=readDatasetMetadata(dir.string(),UiLanguage::Japanese);
    std::vector<IndexProgress> progress;
    ReadFileSlots files;
    test_storage::Index index(0,nullptr,[](void* ctx,const IndexProgress& p){static_cast<std::vector<IndexProgress>*>(ctx)->push_back(p);},&progress,&files);
    const auto final=dir/"_mapindex.dat";
    check(index.rebuild(std::string(config::TemporaryPathBytes,'x'),metadata)==DatasetError::IndexFailed &&
          !files.inUse() && progress.empty() && !fs::exists(final),"oversized transaction path fails before IO or progress");
    check(index.rebuild(dir.string(),metadata)==DatasetError::None,"all-language rebuild succeeds without NVS callback");
    const auto both=read(final);check(both[56]==2,"all declared languages stored, independent of selected language");
    for(const auto* language:{"en","ja"}) {
        DatasetWordBookIndex book;index.loadMap(final.string(),(dir/"map.glc").string(),nullptr,nullptr,&book,language);
        check(book.section_offset && std::string(reinterpret_cast<const char*>(book.header.data()))==language,"language lookup finds the exact stored section");
        (void)index.checkWordBook(book,(dir/(std::string(language)+".giswordbook")).string());
    }
    const auto index_time=fs::last_write_time(final);
    check(index.verifyAgainstDataset(dir.string(),metadata)==DatasetError::None,"read-only whole-source comparison succeeds");
    check(!files.inUse() && read(final)==both && fs::last_write_time(final)==index_time && !fs::exists(dir/"_mapindex.tmp"),
          "verification returns both slots and never writes an index or scratch file");
    check(std::any_of(progress.begin(),progress.end(),[](const auto& p){return p.verifying && p.phase==IndexPhase::Map;}) &&
          std::any_of(progress.begin(),progress.end(),[](const auto& p){return std::string(p.language)=="en";}) && progress.back().phase==IndexPhase::Complete,
          "progress covers generation, source comparison and each language");
    unsigned saves=0;
    metadata=readDatasetMetadata(dir.string(),UiLanguage::English,"en");
    check(index.apply(dir.string(),metadata,[](void* c){++*static_cast<unsigned*>(c);return true;},&saves)==DatasetError::None && saves==1 && read(final)==both,
          "language switch adopts the complete existing index");
    check(fs::last_write_time(final)==index_time,"valid apply never rewrites the index");
    fault=Fault::Write;
    check(index.apply(dir.string(),metadata,[](void*){return true;},nullptr)==DatasetError::None && fault==Fault::Write,
          "valid apply performs no staging writes");fault=Fault::None;
    // One missing language invalidates the entire existing index.
    auto single=metadata;single.language_count=0;
    check(index.rebuild(dir.string(),single)==DatasetError::None && read(final)[56]==1,"single-language fixture");
    progress.clear();
    check(index.apply(dir.string(),metadata,[](void*){return true;},nullptr)==DatasetError::None && read(final)==both,
          "incomplete index is entirely regenerated");
    check(std::any_of(progress.begin(),progress.end(),[](const auto& p){return !p.verifying && p.phase==IndexPhase::Map;}),
          "missing Wordbook forces the GLC to be regenerated too");
    // A CRC-valid map record can be internally consistent yet disagree with the GLC.
    auto mismatch=both;mismatch[64]^=1;
    uint32_t crc=UINT32_MAX;for(size_t i=0;i<mismatch.size()-4;++i){crc^=mismatch[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}crc^=UINT32_MAX;
    for(unsigned i=0;i<4;++i)mismatch[mismatch.size()-4+i]=crc>>(i*8);
    write(final,mismatch);check(index.validate(final.string()),"mismatch fixture retains valid structure/CRC");
    check(index.verifyAgainstDataset(dir.string(),metadata)==DatasetError::IndexMismatch && read(final)==mismatch,"source comparison detects a CRC-valid wrong map record without writing");
    check(index.rebuild(dir.string(),metadata)==DatasetError::None && read(final)==both,"whole rebuild repairs map and all language tables");
    auto bad_book=both;
    const auto wordbook_begin=64+uint32_t(both[60])+(uint32_t(both[61])<<8)+(uint32_t(both[62])<<16)+(uint32_t(both[63])<<24);
    bad_book[wordbook_begin+128+12]^=1;
    crc=UINT32_MAX;for(size_t i=0;i<bad_book.size()-4;++i){crc^=bad_book[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}crc^=UINT32_MAX;
    for(unsigned i=0;i<4;++i)bad_book[bad_book.size()-4+i]=crc>>(i*8);
    write(final,bad_book);check(index.validate(final.string()),"wrong root bit retains valid index structure/CRC");
    check(index.verifyAgainstDataset(dir.string(),metadata)==DatasetError::IndexMismatch && read(final)==bad_book,"source comparison detects a CRC-valid wrong Wordbook root");
    check(index.rebuild(dir.string(),metadata)==DatasetError::None && read(final)==both,"whole rebuild repairs all dictionary tables");
    // Apply checks identity/structure, while explicit rebuild/verification parses bodies.
    auto map_bytes=read(dir/"map.glc");const auto stamp=fs::last_write_time(dir/"map.glc");
    const std::array<uint8_t,4> tag={'G','I','0','1'};auto at=std::search(map_bytes.begin(),map_bytes.end(),tag.begin(),tag.end());check(at!=map_bytes.end(),"GI01 fixture exists");
    const size_t offset=at-map_bytes.begin();auto broken=map_bytes;broken[offset]='X';write(dir/"map.glc",broken);fs::last_write_time(dir/"map.glc",stamp);
    check(index.apply(dir.string(),metadata,[](void*){return true;},nullptr)==DatasetError::None && read(final)==both,
          "complete unchanged index is adopted without rescanning GLC bodies");
    check(index.rebuild(dir.string(),metadata)==DatasetError::IndexFailed && read(final)==both,
          "whole regeneration rejects a damaged GLC even when Wordbooks are unchanged");
    check(index.verifyAgainstDataset(dir.string(),metadata)==DatasetError::IndexMismatch,"full verification does parse the original GLC");
    write(dir/"map.glc",map_bytes);fs::last_write_time(dir/"map.glc",stamp);
    check(index.verifyAgainstDataset(dir.string(),metadata)==DatasetError::None,"full verification recovers after source restoration");
    fs::remove(dir/"ja.giswordbook");
    check(index.rebuild(dir.string(),metadata)==DatasetError::IndexFailed && read(final)==both,"missing nonselected dictionary preserves the previous complete index");
    check(saves==1,"maintenance never invokes settings save");
    std::cout<<"All-language / whole rebuild / read-only source comparison tests passed\n";
}
void testLongLanguageIndex(const fs::path& parent,const fs::path& source_map,const fs::path& source_book) {
    const auto dir=parent/"long-language-index";fs::create_directories(dir);
    fs::copy_file(source_map,dir/"map.glc");fs::copy_file(source_book,dir/"names.giswordbook");
    const std::string a(31,'a'),b=std::string(30,'a')+"b";
    {std::ofstream ini(dir/"dataset.ini");ini<<"[dataset]\nversion=1\nmap=map.glc\ndefault_language="<<a<<"\n["<<b<<"]\ntitle=Second\nwordbook=names.giswordbook\n["<<a<<"]\ntitle=First\nwordbook=names.giswordbook\n";}
    const auto metadata=readDatasetMetadata(dir.string(),UiLanguage::English);
    ReadFileSlots files;test_storage::Index index(0,nullptr,nullptr,nullptr,&files);check(metadata.error==DatasetError::None && index.rebuild(dir.string(),metadata)==DatasetError::None,"31-byte language sections generate a sorted index");
    const auto path=dir/"_mapindex.dat";const auto original=read(path);
    DatasetWordBookIndex first,second;
    index.loadMap(path.string(),(dir/"map.glc").string(),nullptr,nullptr,&first,a);
    index.loadMap(path.string(),(dir/"map.glc").string(),nullptr,nullptr,&second,b);
    check(first.section_offset<second.section_offset && std::string(reinterpret_cast<const char*>(second.header.data()))==b,
          "previous language survives the next record buffer, including a 30-byte shared prefix");
    auto publish=[&](std::vector<uint8_t> data) {
        uint32_t crc=UINT32_MAX;for(size_t i=0;i<data.size()-4;++i){crc^=data[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}crc^=UINT32_MAX;
        for(unsigned i=0;i<4;++i)data[data.size()-4+i]=crc>>(i*8);
        write(path,data);
    };
    auto bad=original;std::fill_n(bad.data()+first.section_offset,31,'z');publish(bad);
    check(!index.validate(path.string()),"CRC-valid descending long language IDs rejected");
    bad=original;std::memcpy(bad.data()+first.section_offset,b.data(),31);publish(bad);
    check(!index.validate(path.string()),"CRC-valid duplicate long language IDs rejected");
    bad=original;bad[first.section_offset+31]='x';publish(bad);
    check(!index.validate(path.string()),"unterminated 32-byte index language rejected before copy");
    write(path,original);check(index.validate(path.string()),"valid long language index recovers");
}
static void testBorrowedIndex(const fs::path& directory,const fs::path& map,const fs::path& book) {
    const auto dir=directory/"borrowed";fs::create_directory(dir);
    fs::copy_file(map,dir/"map.glc");fs::copy_file(book,dir/"book.giswordbook");
    DatasetMetadata metadata;datasetCopyText(metadata.map,sizeof(metadata.map),"map.glc");datasetCopyText(metadata.wordbook,sizeof(metadata.wordbook),"book.giswordbook");datasetCopyText(metadata.language,sizeof(metadata.language),"ja");
    const auto root=dir.string(),path=(dir/"_mapindex.dat").string();
    ReadFileSlots files;test_storage::Index owned(0,nullptr,nullptr,nullptr,&files);
    check(owned.rebuild(root,metadata)==DatasetError::None,"owned control index");const auto expected=read(path);
    static DatasetIndexWorkspace memory;DatasetIndexWork work(memory);
    test_storage::Index borrowed(0,nullptr,nullptr,nullptr,&files,&work);
    for(unsigned operation=0;operation<3;++operation) {
        index_heap::four_k=index_heap::largest=index_heap::calls=index_heap::bytes=0;index_heap::track=true;
        const auto result=operation==0?borrowed.rebuild(root,metadata):operation==1?borrowed.verifyAgainstDataset(root,metadata):
            borrowed.apply(root,metadata,[](void*){return true;},nullptr);
        index_heap::track=false;
        check(result==DatasetError::None && !index_heap::four_k && index_heap::largest<4096,
              "borrowed generation/verification/apply must not allocate IO buffers or a large origin array");
        check(!index_heap::calls,"borrowed maintenance must have zero C++ allocations");
        std::printf("INDEX-HEAP languages=1 operation=%u calls=%zu bytes=%zu\n",operation,index_heap::calls,index_heap::bytes);
        check(!work.inUse() && !files.inUse() && read(path)==expected,"borrowed index bytes and loan release match control");
    }
    DatasetWordBookIndex verified_book;
    borrowed.loadMap(path,(dir/"map.glc").string(),nullptr,nullptr,&verified_book,"ja");
    const auto verified_path=(dir/"book.giswordbook").string();
    index_heap::four_k=index_heap::largest=index_heap::calls=index_heap::bytes=0;index_heap::track=true;
    borrowed.checkWordBook(verified_book,verified_path);
    index_heap::track=false;
    check(!index_heap::largest && !work.inUse() && !files.inUse(),
          "Wordbook header verification skips metadata with zero C++ allocations and returns both loans");
    check(memory.origins[0].x==static_cast<int32_t>(index_format::get(expected.data()+64)) &&
          memory.origins[0].y==static_cast<int32_t>(index_format::get(expected.data()+68)),"map coordinates are written into the borrowed fixed array");
    // Source+Output are live together at a failed write. Both loans must unwind.
    for(const auto failure:{Fault::Write,Fault::Sync,Fault::Close,Fault::Publish}) {
        fault=failure;
        check(borrowed.rebuild(root,metadata)==DatasetError::IndexFailed && fault==Fault::None && !work.inUse() && !files.inUse() && read(path)==expected,
              "borrowed IO/publish failure releases both buffers and preserves the published index");
    }
    check(borrowed.rebuild(root,metadata)==DatasetError::None && !work.inUse() && !files.inUse(),"same workspace recovers after failure");
    std::cout<<"Borrowed index: identical bytes, no 4KiB/origin heap, apply/verify/rebuild and failure recovery passed\n";
}
static void testAllLanguageHeap(const fs::path& directory,const fs::path& map,const fs::path& book) {
    const auto dir=directory/"sixteen-books";fs::create_directory(dir);
    const std::string name=std::string(115,'w')+".giswordbook",first=std::string(30,'a')+"a",last=std::string(30,'a')+"p";
    fs::copy_file(map,dir/"map.glc");fs::copy_file(book,dir/name);
    {std::ofstream ini(dir/"dataset.ini");ini<<"[dataset]\nversion=1\nmap=map.glc\ndefault_language="<<first<<"\n";
     for(int i=15;i>=0;--i)ini<<"["<<std::string(30,'a')<<char('a'+i)<<"]\ntitle=Trial\nwordbook="<<name<<"\n";}
    const auto root=dir.string(),path=(dir/"_mapindex.dat").string();
    auto metadata=readDatasetMetadata(root,UiLanguage::Japanese);
    check(metadata.error==DatasetError::None && metadata.language_count==16,"sixteen-language fixture");
    ReadFileSlots files;test_storage::Index owned(0,nullptr,nullptr,nullptr,&files);
    check(owned.rebuild(root,metadata)==DatasetError::None,"owned sixteen-language control");const auto expected=read(path);
    static DatasetIndexWorkspace memory;DatasetIndexWork work(memory);
    struct ProgressCapture {
        unsigned calls=0;bool first_seen=false,last_seen=false;char last[config::DatasetLanguageBytes]{};
        static void receive(void* ptr,const IndexProgress& p) {
            auto& self=*static_cast<ProgressCapture*>(ptr);++self.calls;
            if(p.language[0]) {
                check(strnlen(p.language,sizeof(p.language))==31,"progress keeps full 31-byte language IDs");
                self.first_seen|=p.language[30]=='a';self.last_seen|=p.language[30]=='p';
                std::memcpy(self.last,p.language,sizeof(self.last));
            }
        }
    } progress;
    test_storage::Index borrowed(0,nullptr,ProgressCapture::receive,&progress,&files,&work);
    for(unsigned operation=0;operation<4;++operation) {
        if(operation==3)fs::remove(path); // Apply with no index must enter Build after releasing its first table loan.
        index_heap::four_k=index_heap::largest=index_heap::calls=index_heap::bytes=0;index_heap::track=true;
        const auto result=operation==0?borrowed.rebuild(root,metadata):operation==1?borrowed.verifyAgainstDataset(root,metadata):
            borrowed.apply(root,metadata,[](void*){return true;},nullptr);
        index_heap::track=false;
        check(result==DatasetError::None && !index_heap::four_k && index_heap::largest<4096,"sixteen-language maintenance borrows IO/origin buffers");
        check(!index_heap::calls,"all-language progress and source collection must be C++ heapless");
        std::printf("INDEX-HEAP languages=16 operation=%u calls=%zu bytes=%zu\n",operation,index_heap::calls,index_heap::bytes);
        check(!work.inUse() && !files.inUse() && read(path)==expected,"all-language result and released table/IO match the control");
    }
    check(progress.calls && progress.first_seen && progress.last_seen,"allocation-free progress reports both extreme sorted languages");
    DatasetWordBookIndex first_book,last_book;
    borrowed.loadMap(path,(dir/"map.glc").string(),nullptr,nullptr,&first_book,first);
    borrowed.loadMap(path,(dir/"map.glc").string(),nullptr,nullptr,&last_book,last);
    check(first_book.section_offset<last_book.section_offset,"all sixteen table records retain sorted order");
    auto& table=work.borrowBooks();table.count=1;datasetCopyText(table.records[0].language,sizeof(table.records[0].language),"retained");
    check(work.inUse(),"table ownership is part of workspace occupancy");
    check(borrowed.rebuild(root,metadata)==DatasetError::IndexFailed && table.count==1 &&
          std::string_view(table.records[0].language)=="retained" && !files.inUse(),"a second borrower cannot reset an active table");
    work.returnBooks(table);check(!work.inUse(),"table can be explicitly returned after rejected reentry");
    // Fewer languages in a subsequent operation must not retain stale records.
    metadata.language_count=0;
    index_heap::calls=index_heap::bytes=0;index_heap::track=true;
    const auto result=borrowed.rebuild(root,metadata);index_heap::track=false;
    check(result==DatasetError::None && !work.inUse() && !files.inUse(),"same workspace recovers and shortens the table");
    check(!index_heap::calls && memory.books.count==1,"one-language reuse has zero allocations and no stale table entries");
    check(index_format::get(read(path).data()+index_format::Header::WordbookCount)==1,"shortened index stores only the new source count");
    std::puts("PASS: sixteen fixed book sources, long names/IDs, byte equality, progress, Apply-to-Build and table reuse");
}
int main(int argc,char** argv) {
    if (argc==3 && std::string(argv[1])=="--validate") {ReadFileSlots files;return test_storage::Index(0,nullptr,nullptr,nullptr,&files).validate(argv[2]) ? 0 : 1;}
    check(argc==4,"usage: index-tests directory map wordbook");
    const fs::path directory=argv[1]; fs::create_directories(directory);
    const auto map=directory/"map.glc",book=directory/"book.giswordbook",final=directory/"_mapindex.dat";
    const auto temporary=directory/"_mapindex.tmp",backup=directory/"_mapindex.bak";
    fs::copy_file(argv[2],map,fs::copy_options::overwrite_existing);
    fs::copy_file(argv[3],book,fs::copy_options::overwrite_existing);
    DatasetMetadata metadata; datasetCopyText(metadata.map,sizeof(metadata.map),map.filename().string()); datasetCopyText(metadata.wordbook,sizeof(metadata.wordbook),book.filename().string()); datasetCopyText(metadata.language,sizeof(metadata.language),"ja");
    ReadFileSlots files;Observer observer;observer.files=&files;test_storage::Index index(0,&observer,nullptr,nullptr,&files); Commit commit{&index,final.string()};
    auto apply=[&] { return index.apply(directory.string(),metadata,Commit::save,&commit); };
    check(apply()==DatasetError::None && commit.calls==1,"initial generation and save");
    const auto good=read(final); check(good.size()>200,"generated index has tables");
    testWordBookIndex(directory.string(),map.string(),book.string());
    const auto loaded=index.loadMap(final.string(),map.string()),original=index.inspectMap(map.string());
    check(loaded.west==original.west && loaded.south==original.south && loaded.width==original.width && loaded.height==original.height,
          "saved metadata matches a source-only scan without retaining a chunk table");
    const auto stamp=fs::last_write_time(map);fs::last_write_time(map,stamp+std::chrono::seconds(60));
    bool stale=false;try{(void)index.loadMap(final.string(),map.string());}catch(const std::runtime_error&){stale=true;}
    check(stale,"changed GLC timestamp invalidates saved index");fs::last_write_time(map,stamp);
    check(!fs::exists(temporary) && !fs::exists(backup),"successful generation leaves no staging files");
    // Second language in the same directory must not destroy the previous index on NVS failure.
    datasetCopyText(metadata.language,sizeof(metadata.language),"en"); commit.success=false;
    check(apply()==DatasetError::SaveFailed && commit.calls==2 && read(final)==good,"failed NVS save restores exact old index");
    commit.success=true; check(apply()==DatasetError::None && read(final)!=good,"language apply regenerates selected section");
    datasetCopyText(metadata.language,sizeof(metadata.language),"ja"); check(apply()==DatasetError::None && read(final)==good,"same map reapply restores deterministic contents");
    const auto calls=commit.calls;
    datasetCopyText(metadata.language,sizeof(metadata.language),"en"); // Force complete rebuild while retaining the valid ja index for rollback.
    for (auto operation : {Fault::Write,Fault::Sync,Fault::Close,Fault::Publish}) {
        fault=operation;
        check(apply()==DatasetError::IndexFailed,"injected IO failure is reported");
        check(fault==Fault::None && commit.calls==calls && read(final)==good,"IO failure preserves index and never saves NVS");
        check(!fs::exists(temporary) && !fs::exists(backup),"failed IO cleans staging and restores final");
    }
    // A changed source must be detected before any rename or selection save.
    observer.change_source=map; observer.temporary=temporary;
    const auto original_time=fs::last_write_time(map);
    check(apply()==DatasetError::IndexFailed && observer.changed && commit.calls==calls && read(final)==good,"source changed during generation is rejected");
    fs::last_write_time(map,original_time); observer.change_source.clear();
    // Truncated map and truncated Wordbook are rejected without fallback.
    const auto map_bytes=read(map),book_bytes=read(book);
    for (const auto& source : {map,book}) {
        fs::resize_file(source,fs::file_size(source)-1);
        check(apply()==DatasetError::IndexFailed && read(final)==good && commit.calls==calls,"truncated source leaves old selection untouched");
        write(source,source==map?map_bytes:book_bytes);
        if (source==map) fs::last_write_time(map,original_time);
    }
    // No previous final: failed save must not leave an unselected new index behind.
    fs::remove(final); commit.success=false;
    check(apply()==DatasetError::SaveFailed && !fs::exists(final) && !fs::exists(backup),"first apply save failure removes newly published index");
    commit.success=true; check(apply()==DatasetError::None,"generation retries after failures");
    const auto current=read(final);
    // Corrupt fields, CRC, truncation, and appended bytes all fail validation.
    auto corrupt=current; corrupt[0]^=1; write(final,corrupt); check(!index.validate(final.string()),"bad magic rejected");
    corrupt=current; corrupt.back()^=1; write(final,corrupt); check(!index.validate(final.string()),"bad CRC rejected");
    corrupt=current; corrupt.resize(corrupt.size()-1); write(final,corrupt); check(!index.validate(final.string()),"short index rejected");
    corrupt=current; corrupt.push_back(0); write(final,corrupt); check(!index.validate(final.string()),"extra index tail rejected");
    // Corrupt existing index is rebuilt on apply, including unchanged sections.
    check(apply()==DatasetError::None && index.validate(final.string()),"invalid existing index triggers full regeneration");
    write(final,current);
    // Publication failure can itself encounter a restore failure: preserve .bak
    // and report this separately rather than claiming the old index is intact.
    datasetCopyText(metadata.language,sizeof(metadata.language),"ja");commit.success=false; fault=Fault::Restore;
    check(apply()==DatasetError::IndexRestoreFailed && !fs::exists(final) && fs::exists(backup),"restore failure retains backup with a distinct error");
    const auto retained_backup=read(backup);
    commit.success=true; fault=Fault::Publish;
    check(apply()==DatasetError::IndexFailed && read(backup)==retained_backup && !fs::exists(final),"a failed retry preserves a leftover backup without adopting it");
    check(apply()==DatasetError::None,"explicit reapply regenerates rather than automatically adopting backup");
    check(!fs::exists(backup) && !fs::exists(temporary),"explicit regeneration clears leftovers after validation");
    // A non-removable staging pathname must fail safely before touching final.
    fs::create_directories(temporary); write(temporary/"occupied",{1}); const auto preserved=read(final);
    check(apply()==DatasetError::None && read(final)==preserved,"valid apply ignores unused staging leftovers");
    check(index.rebuild(directory.string(),metadata)==DatasetError::IndexFailed && read(final)==preserved,
          "explicit rebuild reports staging creation failure without changing final");fs::remove_all(temporary);
    check(!files.inUse() && observer.max_files==2,"index operations use and return at most two fixed file slots");
    check(observer.accesses>0 && observer.depth==0,"all IO observer scopes balance");
    check(index.validate(final.string()),"final index valid after all recovery tests");
    testWordBookHeaders(directory);
    testBorrowedIndex(directory,map,book);
    testAllLanguageHeap(directory,map,book);
    testIndexMaintenance(directory,map,book);
    testLongLanguageIndex(directory,map,book);
    std::cout<<"Dataset index transaction / IO failure tests passed ("<<fs::file_size(final)<<" bytes)\n";
}

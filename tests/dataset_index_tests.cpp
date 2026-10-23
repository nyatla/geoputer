#include "task/storage/dataset/DatasetIndex.h"
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
    void sdBegin() override {
        ++depth; ++accesses;
        if (!changed && !change_source.empty() && fs::exists(temporary) && fs::file_size(temporary)>100) {
            fs::last_write_time(change_source,fs::last_write_time(change_source)+std::chrono::seconds(60)); changed=true;
        }
    }
    void sdEnd() override { check(depth>0,"balanced SD end"); --depth; }
    void cacheState(RgCacheState,bool) override {}
};
struct Commit {
    DatasetIndex* index;
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
    const std::string source(from);
    if ((fault==Fault::Publish && source.find("_mapindex.tmp")!=std::string::npos) ||
        (fault==Fault::Restore && source.find("_mapindex.bak")!=std::string::npos)) {
        fault=Fault::None; errno=EIO; return -1;
    }
    return __real_rename(from,to);
}
}
void testWordBookIndex(const std::string&,const std::string&,const std::string&);
void testIndexMaintenance(const fs::path& parent,const fs::path& source_map,const fs::path& source_book) {
    const auto dir=parent/"maintenance";fs::create_directories(dir);
    fs::copy_file(source_map,dir/"map.glc");fs::copy_file(source_book,dir/"ja.giswordbook");fs::copy_file(source_book,dir/"en.giswordbook");
    {std::ofstream ini(dir/"dataset.ini");ini<<"[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=Japan\nwordbook=ja.giswordbook\n[en]\ntitle=Japan\nwordbook=en.giswordbook\n";}
    auto metadata=readDatasetMetadata(dir.string(),UiLanguage::Japanese);
    std::vector<IndexProgress> progress;
    DatasetIndex index(0,nullptr,[](void* ctx,const IndexProgress& p){static_cast<std::vector<IndexProgress>*>(ctx)->push_back(p);},&progress);
    const auto final=dir/"_mapindex.dat";
    check(index.rebuild(dir.string(),metadata)==DatasetError::None,"all-language rebuild succeeds without NVS callback");
    const auto both=read(final);check(both[56]==2,"all declared languages stored, independent of selected language");
    for(const auto* language:{"en","ja"}) {
        DatasetWordBookIndex book;index.loadMap(final.string(),(dir/"map.glc").string(),nullptr,nullptr,&book,language);
        check(book.section_offset && std::string(reinterpret_cast<const char*>(book.header.data()))==language,"language lookup finds the exact stored section");
        (void)index.checkWordBook(book,(dir/(std::string(language)+".giswordbook")).string());
    }
    const auto index_time=fs::last_write_time(final);
    for(auto target:{IndexTarget::All,IndexTarget::Map,IndexTarget::WordBooks}) {
        check(index.verifyAgainstDataset(dir.string(),metadata,target)==DatasetError::None,"read-only full/partial source comparison succeeds");
        check(read(final)==both && fs::last_write_time(final)==index_time && !fs::exists(dir/"_mapindex.tmp"),"verification never writes an index or scratch file");
    }
    check(std::any_of(progress.begin(),progress.end(),[](const auto& p){return p.verifying && p.phase==IndexPhase::Map;}) &&
          std::any_of(progress.begin(),progress.end(),[](const auto& p){return std::string(p.language)=="en";}) && progress.back().phase==IndexPhase::Complete,
          "progress covers generation, source comparison and each language");
    unsigned saves=0;
    metadata=readDatasetMetadata(dir.string(),UiLanguage::English,"en");
    check(index.apply(dir.string(),metadata,[](void* c){++*static_cast<unsigned*>(c);return true;},&saves)==DatasetError::None && saves==1 && read(final)==both,
          "language switch reuses unchanged sections and preserves both languages");
    // A CRC-valid map record can be internally consistent yet disagree with the GLC.
    auto mismatch=both;mismatch[64]^=1;
    uint32_t crc=UINT32_MAX;for(size_t i=0;i<mismatch.size()-4;++i){crc^=mismatch[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}crc^=UINT32_MAX;
    for(unsigned i=0;i<4;++i)mismatch[mismatch.size()-4+i]=crc>>(i*8);
    write(final,mismatch);check(index.validate(final.string()),"mismatch fixture retains valid structure/CRC");
    check(index.verifyAgainstDataset(dir.string(),metadata)==DatasetError::IndexMismatch && read(final)==mismatch,"source comparison detects a CRC-valid wrong map record without writing");
    check(index.rebuild(dir.string(),metadata,IndexTarget::Map)==DatasetError::None && read(final)==both,"map-only rebuild repairs map while retaining language tables");
    auto bad_book=both;
    const auto wordbook_begin=64+uint32_t(both[60])+(uint32_t(both[61])<<8)+(uint32_t(both[62])<<16)+(uint32_t(both[63])<<24);
    bad_book[wordbook_begin+128+12]^=1;
    crc=UINT32_MAX;for(size_t i=0;i<bad_book.size()-4;++i){crc^=bad_book[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}crc^=UINT32_MAX;
    for(unsigned i=0;i<4;++i)bad_book[bad_book.size()-4+i]=crc>>(i*8);
    write(final,bad_book);check(index.validate(final.string()),"wrong root bit retains valid index structure/CRC");
    check(index.verifyAgainstDataset(dir.string(),metadata)==DatasetError::IndexMismatch && read(final)==bad_book,"source comparison detects a CRC-valid wrong Wordbook root");
    check(index.rebuild(dir.string(),metadata,IndexTarget::WordBooks)==DatasetError::None && read(final)==both,"dictionary-only rebuild repairs all dictionary tables");
    // Prove dictionary-only maintenance does not parse GI01 bodies, even in its comparison pass.
    auto map_bytes=read(dir/"map.glc");const auto stamp=fs::last_write_time(dir/"map.glc");
    const std::array<uint8_t,4> tag={'G','I','0','1'};auto at=std::search(map_bytes.begin(),map_bytes.end(),tag.begin(),tag.end());check(at!=map_bytes.end(),"GI01 fixture exists");
    const size_t offset=at-map_bytes.begin();auto broken=map_bytes;broken[offset]='X';write(dir/"map.glc",broken);fs::last_write_time(dir/"map.glc",stamp);
    check(index.rebuild(dir.string(),metadata,IndexTarget::WordBooks)==DatasetError::None,"dictionary-only rebuild reuses map rather than rescanning GLC bodies");
    check(index.verifyAgainstDataset(dir.string(),metadata)==DatasetError::IndexMismatch,"full verification does parse the original GLC");
    write(dir/"map.glc",map_bytes);fs::last_write_time(dir/"map.glc",stamp);
    check(index.verifyAgainstDataset(dir.string(),metadata)==DatasetError::None,"full verification recovers after source restoration");
    fs::remove(dir/"ja.giswordbook");
    check(index.rebuild(dir.string(),metadata)==DatasetError::IndexFailed && read(final)==both,"missing nonselected dictionary preserves the previous complete index");
    check(saves==1,"maintenance never invokes settings save");
    std::cout<<"All-language / partial rebuild / read-only source comparison tests passed\n";
}
int main(int argc,char** argv) {
    if (argc==3 && std::string(argv[1])=="--validate") return DatasetIndex{}.validate(argv[2]) ? 0 : 1;
    check(argc==4,"usage: index-tests directory map wordbook");
    const fs::path directory=argv[1]; fs::create_directories(directory);
    const auto map=directory/"map.glc",book=directory/"book.giswordbook",final=directory/"_mapindex.dat";
    const auto temporary=directory/"_mapindex.tmp",backup=directory/"_mapindex.bak";
    fs::copy_file(argv[2],map,fs::copy_options::overwrite_existing);
    fs::copy_file(argv[3],book,fs::copy_options::overwrite_existing);
    DatasetMetadata metadata; metadata.map=map.filename().string(); metadata.wordbook=book.filename().string(); metadata.language="ja";
    Observer observer; DatasetIndex index(0,&observer); Commit commit{&index,final.string()};
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
    metadata.language="en"; commit.success=false;
    check(apply()==DatasetError::SaveFailed && commit.calls==2 && read(final)==good,"failed NVS save restores exact old index");
    commit.success=true; check(apply()==DatasetError::None && read(final)!=good,"language apply regenerates selected section");
    metadata.language="ja"; check(apply()==DatasetError::None && read(final)==good,"same map reapply restores deterministic contents");
    const auto calls=commit.calls;
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
    write(final,current);
    // Publication failure can itself encounter a restore failure: preserve .bak
    // and report this separately rather than claiming the old index is intact.
    commit.success=false; fault=Fault::Restore;
    check(apply()==DatasetError::IndexRestoreFailed && !fs::exists(final) && fs::exists(backup),"restore failure retains backup with a distinct error");
    const auto retained_backup=read(backup);
    commit.success=true; fault=Fault::Publish;
    check(apply()==DatasetError::IndexFailed && read(backup)==retained_backup && !fs::exists(final),"a failed retry preserves a leftover backup without adopting it");
    check(apply()==DatasetError::None,"explicit reapply regenerates rather than automatically adopting backup");
    check(!fs::exists(backup) && !fs::exists(temporary),"explicit regeneration clears leftovers after validation");
    // A non-removable staging pathname must fail safely before touching final.
    fs::create_directories(temporary); write(temporary/"occupied",{1}); const auto preserved=read(final);
    check(apply()==DatasetError::IndexFailed && read(final)==preserved,"staging creation failure preserves final"); fs::remove_all(temporary);
    check(observer.accesses>0 && observer.depth==0,"all IO observer scopes balance");
    check(index.validate(final.string()),"final index valid after all recovery tests");
    testIndexMaintenance(directory,map,book);
    std::cout<<"Dataset index transaction / IO failure tests passed ("<<fs::file_size(final)<<" bytes)\n";
}

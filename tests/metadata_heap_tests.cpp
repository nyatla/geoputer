#include "task/storage/dataset/DatasetMetadata.h"
#include "utils/Ellipsis.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>
using namespace geoputer;
namespace {bool tracking=false;size_t allocations=0;}
void* operator new(size_t n) {if(tracking)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n) {return ::operator new(n);}
void operator delete(void* p)noexcept {std::free(p);}
void operator delete[](void* p)noexcept {std::free(p);}
void operator delete(void* p,size_t)noexcept {std::free(p);}
void operator delete[](void* p,size_t)noexcept {std::free(p);}
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Observer:StorageObserver {
    unsigned depth=0,accesses=0;
    void sdBegin()override {++depth;++accesses;}
    void sdEnd()override {check(depth>0,"access balance");--depth;}
    void cacheState(RgCacheState,bool)override {}
};
}
int main() {
    char folder[]="/tmp/rgp-ini-heap-XXXXXX";check(mkdtemp(folder),"temporary directory");
    const auto path=std::string(folder)+"/dataset.ini";
    auto write=[&](const std::string& text) {std::ofstream(path,std::ios::binary)<<text;};
    DatasetMetadata result;DatasetLanguages languages;Observer observer;char title[64],description[256];
    auto read=[&](const char* selected="",int direction=0) {
        allocations=0;tracking=true;
        readDatasetMetadataInto(folder,UiLanguage::English,selected,result,&observer,
            {title,sizeof(title),description,sizeof(description)},direction,&languages);
        tracking=false;
        check(!allocations,"INI parser must perform no C++ dynamic allocations");
        check(!observer.depth,"all file access scopes must finish");
    };
    const std::string id(31,'a');
    std::string ini="[dataset]\nversion=1\nmap="+std::string(123,'m')+".glc\ndefault_language="+id+"\n";
    for(unsigned i=0;i<16;++i) {
        const auto language=i?std::string("x-")+char('a'+i):id;
        ini+="["+language+"]\ntitle="+std::string(60,'t')+"日本語\ndescription="+std::string(243,'d')+"\nwordbook="+std::string(115,'w')+".giswordbook\ndisplay_line1=%0 %1 %%\ndisplay_line2=%2\n";
    }
    write(ini);read();
    check(result.error==DatasetError::None && result.language_count==16 && result.language_index==0 && languages.count==16,"sixteen languages accepted");
    check(std::string_view(result.language)==id && std::strlen(result.map)==127 && std::strlen(result.wordbook)==127,"fixed fields preserve maximum byte lengths and default fallback");
    check(std::string_view(title)==std::string(60,'t')+EllipsisUtf8 && std::strlen(description)==243,"UI strings written directly with UTF-8-safe ellipsis");
    read(id.c_str(),-1);check(result.error==DatasetError::None && result.language_index==15 && std::string_view(result.language)=="x-p","previous wraps to last language");
    read("x-p",1);check(result.language_index==0 && std::string_view(result.language)==id,"next wraps to first language");
    write(ini+"[extra]\ntitle=Extra\nwordbook=extra.giswordbook\n");read();
    check(result.error==DatasetError::Metadata && !languages.count && !title[0],"seventeenth language rejected before publication");
    write(ini+"[x-b]\ntitle=Duplicate\nwordbook=extra.giswordbook\n");read();
    check(result.error==DatasetError::Metadata,"duplicate language rejected, including at capacity");
    write(ini);read("missing");check(result.error==DatasetError::Missing,"missing saved language never falls back");
    write("[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=Name\nwordbook=names.giswordbook\n");
    read();check(result.error==DatasetError::None && !description[0] && !result.display.specified,"successive calls reset missing outputs and formats");
    allocations=0;tracking=true;
    readDatasetMetadataInto(folder,UiLanguage::Japanese,"",result);
    tracking=false;check(!allocations && result.error==DatasetError::None,"execution-only request needs no display output or retained list");
    write("[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=\xc0\x80\nwordbook=names.giswordbook\n");read();
    check(result.error==DatasetError::Metadata,"invalid UTF-8 is rejected without heap use");
    std::filesystem::remove_all(folder);
    std::puts("PASS: INI C++ allocations=0; 16/17 languages, duplicates, long IDs/names, direct UTF-8 output, switching, reset and failures");
}

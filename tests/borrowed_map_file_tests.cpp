#include "support/MapIndexFixture.h"
#include "task/storage/MapBuildJob.h"
#include "task/storage/galuchat/MapSource.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <new>
#include <cstring>
using namespace geoputer;
namespace {bool track=false;size_t four_k=0,allocated=0;void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
void* operator new(size_t n){if(track){four_k+=n==4096;allocated+=n;}if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&) noexcept {try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void*p,const std::nothrow_t&) noexcept {std::free(p);}
void operator delete[](void*p,const std::nothrow_t&) noexcept {std::free(p);}

int main(int argc,char**argv){try{
    if(argc!=4)return 2;
    namespace fs=std::filesystem;const fs::path dir=argv[1];fs::create_directories(dir);
    fs::create_symlink(fs::absolute(argv[2]),dir/"map.glc");fs::create_symlink(fs::absolute(argv[3]),dir/"names.giswordbook");
    std::ofstream(dir/"dataset.ini")<<"[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=Trial\nwordbook=names.giswordbook\n";
    test_index::generate(dir.string(),"map.glc","names.giswordbook");
    for(unsigned scale:{1u,2u,4u}){
        map::Request req{};req.token=scale;req.screen_generation=1;req.dataset_generation=1;req.latitude=35.681641;req.longitude=140.027194;req.scale=scale;
        map::FixedBoundaryImage output;req.buffer={output.storageData(),output.Capacity,1};
        map::MapSession session;MapBuildJob job;check(job.start(req,session),"job start");
        four_k=allocated=0;track=true;auto owned=readBoundaryMap(dir.string(),"ja",req,0,nullptr);track=false;const auto owned_four=four_k,owned_bytes=allocated;
        four_k=allocated=0;track=true;auto borrowed=readBoundaryMap(dir.string(),"ja",req,0,nullptr,{},job.indexReadBuffer());track=false;
        check(owned.info.error==map::Error::None && borrowed.info.error==map::Error::None,"map load");
        check(owned_four==four_k+1 && owned_bytes==allocated+4096,"pipeline must remove exactly one 4KiB allocation");
        check(!job.indexReadBuffer()->inUse(),"index Reader escaped the synchronous source read");
        check(owned.info.region==borrowed.info.region && owned.view().count==borrowed.view().count && !std::memcmp(owned.pixels.get(),borrowed.pixels.get(),owned.bytes()),"decoded region raster differs");
        map::BoundaryImage reference;check(reference.resize(owned.info.width,owned.info.height),"reference image");
        check(map::buildBoundaryRows(owned.view(),owned.plan,owned.info.region,0,owned.info.height,reference)==map::Error::None,"reference render");
        job.loaded(std::move(borrowed));check(!job.indexReadBuffer(),"rendering must not lend its pixels");
        while(!job.complete())job.step();
        check(job.info().error==map::Error::None,"borrowed render");
        check(!std::memcmp(req.buffer.data,reference.data(),reference.bytes()),"boundary bytes differ after borrowed memory overwritten");
        check(job.release(req),"release");
        ++req.token;req.reuse=true;req.region=0;req.region_known=true;check(job.start(req,session),"recolor");
        while(!job.complete())job.step();
        check(job.info().error==map::Error::None && job.release(req),"repeat use");
        std::cout<<"PASS: scale=1/"<<scale<<" 4KiB_allocations="<<owned_four<<"->"<<four_k<<" cpp_allocation_bytes="<<owned_bytes<<"->"<<allocated<<" region_pixels="<<owned.view().count<<" equal raster/boundary, scratch released before rendering\n";
    }
}catch(const std::exception&e){track=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

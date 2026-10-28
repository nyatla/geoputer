#include "support/MapIndexFixture.h"
#include "task/storage/MapBuildJob.h"
#include "task/storage/galuchat/PlaceLookup.h"
#include "task/storage/galuchat/MapSource.h"
#include "task/storage/ReadFile.h"
#include "task/storage/galuchat/ObservedFileReader.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <new>
#include <cstring>
using namespace geoputer;
namespace {bool track=false;size_t four_k=0,allocated=0,reader_allocs=0;void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
void* operator new(size_t n){if(track){four_k+=n==4096;allocated+=n;reader_allocs+=n==sizeof(ObservedFileReader);}if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
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
    CodeCacheBuffer lookup_pixels{};SelectedDataset selected;PlaceLookup lookup(lookup_pixels,selected);
    lookup.configure(dir.string(),"ja");check(lookup.prepare().state==PlaceState::Ready,"prepare shared dataset");
    fs::rename(dir/"dataset.ini",dir/"dataset.hidden"); // Map reads cannot depend on INI after initialization.
    for(unsigned scale:{1u,2u,4u}){
        map::Request req{};req.token=scale;req.screen_generation=1;req.dataset_generation=selected.info.generation;req.latitude=35.681641;req.longitude=140.027194;req.scale=scale;
        map::FixedBoundaryImage output;req.buffer={output.storageData(),output.Capacity,1};
        map::CodeImageBuffer session_pixels{};map::MapSession session(session_pixels);MapBuildJob job;check(job.start(req,session),"job start");
        four_k=allocated=0;track=true;map::CodeImageBuffer reference_pixels{};auto owned=readBoundaryMap(selected,req,0,nullptr,map::MapSession(reference_pixels));track=false;const auto owned_four=four_k,owned_bytes=allocated;
        four_k=allocated=reader_allocs=0;track=true;auto borrowed=readBoundaryMap(selected,req,0,nullptr,std::move(session),job.readBuffer(),job.readFile(),job.pathBuffer());track=false;
        check(owned.info.error==map::Error::None && borrowed.info.error==map::Error::None,"map load");
        check(!reader_allocs && !job.readFile()->inUse(),"map generation allocated a Reader or retained the FIL");
        check(owned_four==four_k+2 && owned_bytes==allocated+8192,"index/GLC reads must borrow two sequential 4KiB buffers");
        check(!job.readBuffer()->inUse(),"precheck/index/GLC Reader escaped the synchronous source read");
        check(owned.info.region==borrowed.info.region && owned.view().count==borrowed.view().count && !std::memcmp(owned.pixels,borrowed.pixels,owned.bytes()),"decoded region raster differs");
        map::BoundaryImage reference;check(reference.resize(owned.info.width,owned.info.height),"reference image");
        check(map::buildBoundaryRows(owned.view(),owned.plan,owned.info.region,0,owned.info.height,reference)==map::Error::None,"reference render");
        job.loaded(std::move(borrowed));check(!job.readBuffer() && !job.pathBuffer().supplied(),"rendering must not lend the ended read layout");
        while(!job.complete())job.step();
        check(job.info().error==map::Error::None,"borrowed render");
        check(!std::memcmp(req.buffer.data,reference.data(),reference.bytes()),"boundary bytes differ after borrowed memory overwritten");
        check(job.release(req),"release");
        ++req.token;req.reuse=true;req.region=0;req.region_known=true;check(job.start(req,session),"recolor");
        while(!job.complete())job.step();
        check(job.info().error==map::Error::None && job.release(req),"repeat use");
        std::cout<<"PASS: scale=1/"<<scale<<" 4KiB_allocations="<<owned_four<<"->"<<four_k<<" cpp_allocation_bytes="<<owned_bytes<<"->"<<allocated<<" region_pixels="<<owned.view().count<<" equal raster/boundary, scratch released before rendering\n";
    }
    // Missing/invalid index data must return the loan before releasing the job.
    fs::resize_file(dir/"_mapindex.dat",0);
    map::Request bad{};bad.token=99;bad.screen_generation=1;bad.dataset_generation=selected.info.generation;
    bad.latitude=35.681641;bad.longitude=140.027194;bad.scale=1;
    map::FixedBoundaryImage output;bad.buffer={output.storageData(),output.Capacity,99};
    map::CodeImageBuffer pixels{};map::MapSession session(pixels);MapBuildJob job;
    check(job.start(bad,session),"failed-precheck job start");
    auto failed=readBoundaryMap(selected,bad,0,nullptr,std::move(session),job.readBuffer(),job.readFile(),job.pathBuffer());
    check(!job.readFile()->inUse(),"failed precheck retained FIL");
    check(failed.info.error==map::Error::ReadError && !job.readBuffer()->inUse(),"failed precheck retained the loan");
    job.loaded(std::move(failed));check(job.release(bad),"release after failed precheck");
    std::cout<<"PASS: invalid index releases shared memory\n";
    lookup.close();check(!selected.ready() && selected.map_path.empty(),"close invalidates shared paths");
    check(lookup.prepare().state==PlaceState::DatasetMissing,"revalidation must reread missing INI");
    const auto old_generation=selected.info.generation;
    fs::rename(dir/"dataset.hidden",dir/"dataset.ini");
    test_index::generate(dir.string(),"map.glc","names.giswordbook");
    lookup.close();check(lookup.prepare().state==PlaceState::Ready && selected.info.generation!=old_generation,"revalidation reloads shared dataset");
    check(readBoundaryMap(selected,bad,0,nullptr,map::MapSession(pixels)).info.error==map::Error::Stale,"reject old dataset generation");
    std::cout<<"PASS: one initialization, INI-free map reads, explicit revalidation and stale generation rejection\n";
}catch(const std::exception&e){track=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

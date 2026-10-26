#include "support/MapIndexFixture.h"
#include "map/MapFrameReceiver.h"
#include "task/storage/galuchat/PlaceLookup.h"
#include "utils/FileReadBuffer.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <new>
#include <cstring>
using namespace geoputer;
namespace {
bool track=false;size_t allocated=0,allocations=0;
void check(bool ok,const char* why){if(!ok){track=false;throw std::runtime_error(why);}}
}
void* operator new(size_t n){if(track){allocated+=n;++allocations;}if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void*p,const std::nothrow_t&)noexcept{std::free(p);}void operator delete[](void*p,const std::nothrow_t&)noexcept{std::free(p);}
int main(int argc,char**argv){try{
    if(argc!=4)return 2;
    namespace fs=std::filesystem;const fs::path dir=argv[1];fs::create_directories(dir);
    fs::create_symlink(fs::absolute(argv[2]),dir/"map.glc");fs::create_symlink(fs::absolute(argv[3]),dir/"names.giswordbook");
    std::ofstream(dir/"dataset.ini")<<"[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=Trial\nwordbook=names.giswordbook\n";
    test_index::generate(dir.string(),"map.glc","names.giswordbook");
    PlaceLookup own,borrowed;own.configure(dir.string(),"ja");borrowed.configure(dir.string(),"ja");
    check(own.prepare().state==PlaceState::Ready && borrowed.prepare().state==PlaceState::Ready,"prepare real lookup pair");
    map::MapFrameReceiver ui;size_t own_bytes=0,borrowed_bytes=0,own_count=0,borrowed_count=0;unsigned number=0;
    for(double offset:{0.,0.00001,0.03,0.06,0.03,0.,0.}) {
        PlaceRequest request{1,++number,{35.681641,140.027194+offset,0,true}};
        request.token=number;request.dataset_generation=borrowed.datasetInfo().generation;request.work_bytes=config::IndexReadBufferBytes;
        check(ui.beginWork(request.token,request.buffer) && validPlaceMemory(request),"lend fixed UI back");
        auto* result=new (placeResultMemory(request)) PlaceResult{};
        allocated=allocations=0;track=true;const auto expected=own.search(request);track=false;
        own_bytes+=allocated;own_count+=allocations;
        allocated=allocations=0;track=true;
        {BorrowedReadBuffer work(request.buffer.data,request.work_bytes);borrowed.searchInto(request,*result,&work);check(!work.inUse(),"Reader must end before publishing completion");}
        track=false;borrowed_bytes+=allocated;borrowed_count+=allocations;
        check(result->state==expected.state && result->region_code==expected.region_code &&
              !std::memcmp(result->lines,expected.lines,sizeof(result->lines)) &&
              result->dataset.state==expected.dataset.state && samePlaceRequest(result->request,request),"owned/borrowed lookup results differ");
        result->~PlaceResult();check(ui.returnWork(request.token,request.buffer),"return UI work after successful query");
    }
    check(borrowed_bytes<own_bytes && borrowed_count<own_count,"borrowing must remove index-buffer allocations");
    // The persistent factory must not keep a reference to a finished stack lease.
    const auto legacy=borrowed.search({1,99,{35.69,140.01,0,true}});
    check(legacy.state==PlaceState::Ready || legacy.state==PlaceState::NotFound,"legacy self-owned search after borrowed calls");
    // Truncate the index after open: force an exception under an active binding.
    fs::resize_file(dir/"_mapindex.dat",0);
    PlaceRequest broken{1,100,{35.70,139.76,0,true}};broken.token=100;broken.dataset_generation=borrowed.datasetInfo().generation;broken.work_bytes=config::IndexReadBufferBytes;
    check(ui.beginWork(broken.token,broken.buffer),"lend scratch for read failure");
    auto* error=new (placeResultMemory(broken)) PlaceResult{};
    {BorrowedReadBuffer work(broken.buffer.data,broken.work_bytes);borrowed.searchInto(broken,*error,&work);check(!work.inUse(),"exception leaked an active Reader loan");}
    check(error->state==PlaceState::DataError && !error->lines[0][0],"failed lookup must not publish stale text");
    error->~PlaceResult();check(ui.returnWork(broken.token,broken.buffer),"failed query returns fixed memory");
    std::cout<<"PASS: real place scratch/result, 7 queries, allocations="<<own_count<<"->"<<borrowed_count
             <<" cpp_bytes="<<own_bytes<<"->"<<borrowed_bytes<<", cache/name/code equality, factory reset and exception cleanup\n";
}catch(const std::exception&e){track=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

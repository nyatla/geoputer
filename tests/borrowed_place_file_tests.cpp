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
bool track=false;size_t allocated=0,allocations=0,four_k=0;
void check(bool ok,const char* why){if(!ok){track=false;throw std::runtime_error(why);}}
}
void* operator new(size_t n){if(track){allocated+=n;++allocations;four_k+=n==4096;}if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
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
    CodeCacheBuffer own_pixels{},borrowed_pixels{};SelectedDataset own_dataset,borrowed_dataset;PlaceLookup own(own_pixels,own_dataset),borrowed(borrowed_pixels,borrowed_dataset);own.configure(dir.string(),"ja");borrowed.configure(dir.string(),"ja");
    check(own.prepare().state==PlaceState::Ready && borrowed.prepare().state==PlaceState::Ready,"prepare real lookup pair");
    map::MapFrameReceiver ui;size_t own_bytes=0,borrowed_bytes=0,own_count=0,borrowed_count=0;unsigned number=0;
    for(double offset:{0.,0.00001,0.03,0.06,0.03,0.,0.}) {
        PlaceRequest request{1,++number,{35.681641,140.027194+offset,0,true}};
        request.token=number;request.dataset_generation=borrowed.datasetInfo().generation;request.work_bytes=config::IndexReadBufferBytes;
        check(ui.beginWork(request.token,request.buffer) && validPlaceMemory(request),"lend fixed UI back");
        auto* result=beginPlaceMemory(request);
        const auto old_fills=own.cacheStats().fills,old_book_misses=own.wordBookCacheStats().misses;
        allocated=allocations=four_k=0;track=true;const auto expected=own.search(request);track=false;
        const auto own_four=four_k;
        const auto fills=own.cacheStats().fills-old_fills,book_misses=own.wordBookCacheStats().misses-old_book_misses;
        own_bytes+=allocated;own_count+=allocations;
        allocated=allocations=four_k=0;track=true;
        {auto& bytes=placeReaderMemory(request);BorrowedReadBuffer work(bytes.data(),bytes.size());borrowed.searchInto(request,*result,&work);check(!work.inUse(),"Reader must end before publishing completion");}
        track=false;borrowed_bytes+=allocated;borrowed_count+=allocations;
        // Wordbook index ranges may be shorter than 4KiB. Check the map
        // allocation delta independently of the Wordbook index/body buffers.
        check(!four_k,"all borrowed lookup Readers must avoid 4KiB allocations");
        check(own_four>=four_k+2*fills,"GLC cache fills must borrow both index and map buffers");
        if(!book_misses)check(own_four==four_k+2*fills,"cached names must add no other 4KiB borrow delta");
        check(result->state==expected.state && result->region_code==expected.region_code &&
              !std::memcmp(result->lines,expected.lines,sizeof(result->lines)) &&
              result->dataset.state==expected.dataset.state && samePlaceRequest(result->request,request),"owned/borrowed lookup results differ");
        endPlaceMemory(request);check(ui.returnWork(request.token,request.buffer),"return UI work after successful query");
    }
    check(borrowed_bytes<own_bytes && borrowed_count<own_count,"borrowing must remove index/GLC/Wordbook buffer allocations");
    // The persistent factory must not keep a reference to a finished stack lease.
    const auto legacy=borrowed.search({1,99,{35.69,140.01,0,true}});
    check(legacy.state==PlaceState::Ready || legacy.state==PlaceState::NotFound,"legacy self-owned search after borrowed calls");
    // Fail GLC I/O after its factory has been prepared, without touching the
    // original dataset behind the symlink. Both factory bindings must unwind.
    fs::rename(dir/"map.glc",dir/"map.saved");std::ofstream(dir/"map.glc",std::ios::binary).put('x');
    PlaceRequest bad_map{1,100,{35.681641,140.027194,0,true}};bad_map.token=100;
    bad_map.dataset_generation=borrowed.datasetInfo().generation;bad_map.work_bytes=config::IndexReadBufferBytes;
    check(ui.beginWork(bad_map.token,bad_map.buffer),"lend for GLC failure");
    auto* map_error=beginPlaceMemory(bad_map);
    {auto& bytes=placeReaderMemory(bad_map);BorrowedReadBuffer work(bytes.data(),bytes.size());borrowed.searchInto(bad_map,*map_error,&work);check(!work.inUse(),"GLC failure leaked loan");}
    check(map_error->state==PlaceState::DataError,"truncated GLC unexpectedly succeeded");
    endPlaceMemory(bad_map);check(ui.returnWork(bad_map.token,bad_map.buffer),"return after GLC failure");
    fs::remove(dir/"map.glc");fs::rename(dir/"map.saved",dir/"map.glc");
    borrowed.close();check(borrowed.prepare().state==PlaceState::Ready,"reopen after GLC failure");
    const auto recovered=borrowed.search({1,101,{35.681641,140.027194,0,true}});
    check(recovered.state==PlaceState::Ready,"self-owned search after failed GLC borrowing");
    // Truncate the index after open: force an exception under an active binding.
    fs::resize_file(dir/"_mapindex.dat",0);
    PlaceRequest broken{1,100,{35.70,139.76,0,true}};broken.token=100;broken.dataset_generation=borrowed.datasetInfo().generation;broken.work_bytes=config::IndexReadBufferBytes;
    check(ui.beginWork(broken.token,broken.buffer),"lend scratch for read failure");
    auto* error=beginPlaceMemory(broken);
    {auto& bytes=placeReaderMemory(broken);BorrowedReadBuffer work(bytes.data(),bytes.size());borrowed.searchInto(broken,*error,&work);check(!work.inUse(),"exception leaked an active Reader loan");}
    check(error->state==PlaceState::DataError && !error->lines[0][0],"failed lookup must not publish stale text");
    endPlaceMemory(broken);check(ui.returnWork(broken.token,broken.buffer),"failed query returns fixed memory");
    std::cout<<"PASS: real place scratch/result, 7 queries, allocations="<<own_count<<"->"<<borrowed_count
             <<" cpp_bytes="<<own_bytes<<"->"<<borrowed_bytes<<", cache/name/code equality, factory reset and exception cleanup\n";
}catch(const std::exception&e){track=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

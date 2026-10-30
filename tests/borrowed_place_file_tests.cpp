#include "support/StorageFixture.h"
#include "support/MapIndexFixture.h"
#include "task/storage/ReadFile.h"
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
    namespace fs=std::filesystem;const std::string dataset_root=argv[1];const fs::path dir=fs::path(dataset_root)/"01";fs::create_directories(dir);
    fs::create_symlink(fs::absolute(argv[2]),dir/"map.glc");fs::create_symlink(fs::absolute(argv[3]),dir/"names.giswordbook");
    std::ofstream(dir/"dataset.ini")<<"[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=Trial\nwordbook=names.giswordbook\n";
    test_index::generate(dir.string(),"map.glc","names.giswordbook");
    ReadFileSlots files;
    CodeCacheBuffer own_pixels{},borrowed_pixels{};SelectedDataset own_dataset,borrowed_dataset;test_storage::Lookup own(own_pixels,own_dataset,0,dataset_root),borrowed(borrowed_pixels,borrowed_dataset,0,dataset_root);own.configure(1,"ja");borrowed.configure(1,"ja");borrowed.setFiles(&files);
    const std::string long_language(31,'a');
    PlaceDisplayFormat retained{{"%0 %1%2%3","%4%5"},true};
    allocated=allocations=four_k=0;track=true;
    borrowed.configure(1,long_language);borrowed_dataset.display=retained;
    borrowed.configure(1,"ja");
    track=false;
    check(!allocations && borrowed.selectedLanguage()=="ja" && !borrowed_dataset.display.specified,
          "selection language and format retention/reset perform no C++ allocations");
    borrowed.configure(1,std::string(32,'a'));
    check(borrowed.datasetInfo().state==PlaceState::MetadataError,"oversized requested language is rejected before file access");
    borrowed.configure(1,"ja");
    static DatasetIndexWorkspace init_memory;DatasetIndexWork init_work(init_memory);
    PlaceResult prepared;allocated=allocations=four_k=0;track=true;
    borrowed.prepareInto(prepared,init_work.available());track=false;
    check(!allocations && !four_k && !init_work.inUse() && !files.inUse(),"initialization sequentially borrows 4KiB for GLC/index/Wordbook and returns loans");
    check(own.prepare().state==PlaceState::Ready && prepared.state==PlaceState::Ready,"prepare real lookup pair, including direct destination");
    // Nine-point alert monitoring uses only hierarchy identifiers, never the
    // display string decoder or a second map image. Exercise every real depth.
    map::MapFrameReceiver alarm_work;
    const auto depth=index_format::get16(borrowed_dataset.book_index.header.data()+index_format::Wordbook::Depth);
    const auto names_before=borrowed.wordBookCacheStats();
    for(unsigned level=1;level<=depth;++level) {
        PlaceRequest request{1,900+level,{35.681641,140.027194,0,true}};
        request.token=900+level;request.alarm_sample=true;request.dataset_generation=borrowed.datasetInfo().generation;request.work_bytes=config::IndexReadBufferBytes;
        check(alarm_work.beginWork(request.token,request.buffer),"lend same UI back for alarm sample");
        auto* result=beginPlaceMemory(request);auto& bytes=placeReaderMemory(request);BorrowedReadBuffer work(bytes.data(),bytes.size());
        alarm::Settings settings;settings.enabled=true;settings.level=level;
        const auto cell_before=borrowed.cacheStats();
        allocated=allocations=four_k=0;track=true;borrowed.observeAlarmInto(request,*result,settings,&work);track=false;
        check(result->state==PlaceState::Ready && result->alarm.center.depth==level && result->alarm.valid_points>0 &&
              result->alarm.agreeing>=1 && result->alarm.agreeing<=result->alarm.valid_points,"real nine-point lookup returns selected parent path and votes");
        check(!allocations && !four_k && !work.inUse() && !files.inUse(),"alarm hierarchy lookup is heapless and returns FIL/buffer loans");
        const auto cell_after=borrowed.cacheStats();
        check((cell_after.hits-cell_before.hits)+(cell_after.misses-cell_before.misses)==9,"standard samples exactly center and eight adjacent source cells");
        settings.judgement=alarm::Judgement::Immediate;
        allocated=allocations=four_k=0;track=true;borrowed.observeAlarmInto(request,*result,settings,&work);track=false;
        const auto one=borrowed.cacheStats();
        check(!allocations && result->alarm.valid_points==1 && result->alarm.agreeing==1 &&
              (one.hits-cell_after.hits)+(one.misses-cell_after.misses)==1,"immediate samples one cell and remains heapless");
        endPlaceMemory(request);check(alarm_work.returnWork(request.token,request.buffer),"return alarm workspace");
    }
    const auto names_after=borrowed.wordBookCacheStats();
    check(names_before.hits==names_after.hits && names_before.misses==names_after.misses,"alarm path never generates or caches display strings");
    std::cout<<"PASS: real nine-point hierarchy samples, levels="<<depth<<", C++ allocations=0, no display-wordbook lookups\n";
    ReadFile retained_log(0,&files.primary());check(retained_log.openRead((dir/"names.giswordbook").string()),"reserve primary FIL as a retained log read");
    map::MapFrameReceiver ui;size_t own_bytes=0,borrowed_bytes=0,own_count=0,borrowed_count=0;unsigned number=0;
    for(double offset:{0.,0.00001,0.03,0.06,0.03,0.,0.}) {
        PlaceRequest request{1,++number,{35.681641,140.027194+offset,0,true}};
        request.token=number;request.dataset_generation=borrowed.datasetInfo().generation;request.work_bytes=config::IndexReadBufferBytes;
        check(ui.beginWork(request.token,request.buffer) && validPlaceMemory(request),"lend fixed UI back");
        auto* result=beginPlaceMemory(request);

        allocated=allocations=four_k=0;track=true;const auto expected=own.search(request);track=false;
        const auto own_four=four_k;

        own_bytes+=allocated;own_count+=allocations;
        allocated=allocations=four_k=0;track=true;
        {auto& bytes=placeReaderMemory(request);BorrowedReadBuffer work(bytes.data(),bytes.size());borrowed.searchInto(request,*result,&work,&placeWordBookMemory(request));check(!work.inUse(),"Reader must end before publishing completion");}
        track=false;borrowed_bytes+=allocated;borrowed_count+=allocations;
        // Wordbook index ranges may be shorter than 4KiB. Check the map
        // allocation delta independently of the Wordbook index/body buffers.
        check(!four_k && !allocations,"normal borrowed lookup must be C++ heapless");
        check(!own_four,"test caller also supplies fixed Reader memory");
        check(result->state==expected.state && result->region_code==expected.region_code &&
              !std::memcmp(result->lines,expected.lines,sizeof(result->lines)) &&
              result->dataset.state==expected.dataset.state && samePlaceRequest(result->request,request),"owned/borrowed lookup results differ");
        check(!files.secondary().inUse() && files.primary().inUse(),"place lookup returns its secondary FIL while primary remains owned by log browsing");
        endPlaceMemory(request);check(ui.returnWork(request.token,request.buffer),"return UI work after successful query");
    }
    check(retained_log.close() && !files.inUse(),"all fixed FIL loans returned");
    check(!borrowed_bytes && !own_bytes && !borrowed_count && !own_count,"both caller-owned query layouts are heapless");
    // Persistent file descriptions must not retain a finished per-call stack lease.
    const auto next_query=borrowed.search({1,99,{35.69,140.01,0,true}});
    check(next_query.state==PlaceState::Ready || next_query.state==PlaceState::NotFound,"independent per-call workspace after borrowed calls");
    // Fail GLC I/O after the file descriptions are prepared, without touching the
    // original dataset behind the symlink. All scoped Readers must unwind.
    fs::rename(dir/"map.glc",dir/"map.saved");std::ofstream(dir/"map.glc",std::ios::binary).put('x');
    PlaceRequest bad_map{1,100,{35.681641,140.027194,0,true}};bad_map.token=100;
    bad_map.dataset_generation=borrowed.datasetInfo().generation;bad_map.work_bytes=config::IndexReadBufferBytes;
    check(ui.beginWork(bad_map.token,bad_map.buffer),"lend for GLC failure");
    auto* map_error=beginPlaceMemory(bad_map);
    {auto& bytes=placeReaderMemory(bad_map);BorrowedReadBuffer work(bytes.data(),bytes.size());borrowed.searchInto(bad_map,*map_error,&work,&placeWordBookMemory(bad_map));check(!work.inUse(),"GLC failure leaked loan");}
    check(map_error->state==PlaceState::DataError,"truncated GLC unexpectedly succeeded");
    endPlaceMemory(bad_map);check(ui.returnWork(bad_map.token,bad_map.buffer),"return after GLC failure");
    fs::remove(dir/"map.glc");fs::rename(dir/"map.saved",dir/"map.glc");
    borrowed.close();check(borrowed.prepare().state==PlaceState::Ready,"reopen after GLC failure");
    const auto recovered=borrowed.search({1,101,{35.681641,140.027194,0,true}});
    check(recovered.state==PlaceState::Ready,"query recovery with caller-owned workspace");
    // Truncate the index after open: force an exception under an active binding.
    fs::resize_file(dir/"_mapindex.dat",0);
    PlaceRequest broken{1,100,{35.70,139.76,0,true}};broken.token=100;broken.dataset_generation=borrowed.datasetInfo().generation;broken.work_bytes=config::IndexReadBufferBytes;
    check(ui.beginWork(broken.token,broken.buffer),"lend scratch for read failure");
    auto* error=beginPlaceMemory(broken);
    {auto& bytes=placeReaderMemory(broken);BorrowedReadBuffer work(bytes.data(),bytes.size());borrowed.searchInto(broken,*error,&work,&placeWordBookMemory(broken));check(!work.inUse(),"exception leaked an active Reader loan");}
    check(error->state==PlaceState::DataError && !error->lines[0][0],"failed lookup must not publish stale text");
    endPlaceMemory(broken);check(ui.returnWork(broken.token,broken.buffer),"failed query returns fixed memory");
    std::cout<<"PASS: real place scratch/result, 7 queries, allocations="<<borrowed_count
             <<" cpp_bytes="<<borrowed_bytes<<", cache/name/code equality, per-call loans and exception cleanup\n";
}catch(const std::exception&e){track=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

#include "support/MapIndexFixture.h"
#include "map/MapFrameReceiver.h"
#include "task/storage/galuchat/MapSource.h"
#include "task/storage/galuchat/PlaceLookup.h"
#include "task/storage/MapBuildJob.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
#include <cstring>
using namespace geoputer;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Observer:StorageObserver {
    unsigned sd=0,rg=0;
    void sdBegin() override{++sd;}
    void sdEnd() override{}
    void cacheState(RgCacheState,bool=false) override{++rg;}
};
int main(int argc,char** argv) {
    try {
        if(argc!=4)return 2;
        namespace fs=std::filesystem;const fs::path dir=argv[1];fs::create_directories(dir);
        fs::copy_file(argv[2],dir/"map.glc");fs::copy_file(argv[3],dir/"names.giswordbook");
        std::ofstream(dir/"dataset.ini")<<"[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=Test\nwordbook=names.giswordbook\n";
        test_index::generate(dir.string(),"map.glc","names.giswordbook");
        Observer observer;PlaceLookup lookup;lookup.setObserver(&observer);lookup.configure(dir.string(),"ja");
        check(lookup.prepare().state==PlaceState::Ready,"existing lookup opens normally");
        PlaceRequest point{1,1,{35.681641,140.027194,1000,true}};
        const auto place=lookup.search(point);check(place.state==PlaceState::Ready,"existing 128 cache populated");
        const auto before=lookup.cacheStats();const auto generation=lookup.datasetInfo().generation;const auto rg=observer.rg;
        map::Request request{};request.token=19;request.screen_generation=4;request.dataset_generation=generation;
        request.latitude=point.fix.latitude;request.longitude=point.fix.longitude;
        const auto started=std::chrono::steady_clock::now();
        auto result=readBoundaryMap(dir.string(),"ja",request,0,&observer);
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        check(result.info.error==map::Error::None && result.pixels,"independent map source succeeds");
        check(result.info.region==place.region_code && result.info.name[0],"map center and name match the existing point lookup");
        check(result.plan.source.width==240 && result.plan.source.height<=101,"viewport fits the independent work limit");
        const auto after=lookup.cacheStats();
        check(after.bytes==before.bytes && after.fills==before.fills && after.hits==before.hits &&
              after.misses==before.misses && after.bounds.x==before.bounds.x && after.bounds.y==before.bounds.y &&
              lookup.datasetInfo().generation==generation && observer.rg==rg,"map never invalidates, reuses or changes the 128 cache or RG state");
        const auto raw_bytes=result.view().count*4;const auto name=std::string(result.info.name);
        MapBuildJob job;job.start(request);job.loaded(std::move(result));
        unsigned steps=0;while(!job.complete() && ++steps<20)job.step();
        check(job.complete() && job.info().error==map::Error::None,"map renders in bounded steps");
        map::MapFrameReceiver receiver;check(receiver.begin(job.info()),"frame receiver allocated");
        map::Transfer transfer{};
        for(unsigned row=0;row<101;row+=8){request.row=row;check(job.read(request,transfer) && receiver.write(transfer),"full map transfers in ordered row messages");}
        check(receiver.commit(job.info()) && receiver.image().bytes()==6060,"map is complete and compact");
        job.release(request);
        const auto accesses=observer.sd;
        const auto repeated=lookup.search(point);
        check(repeated.state==PlaceState::Ready && repeated.region_code==place.region_code && observer.sd==accesses && lookup.cacheStats().fills==before.fills,
              "original map and name caches still hit without SD IO after independent rendering");
        fs::remove(dir/"_mapindex.dat");
        check(readBoundaryMap(dir.string(),"ja",request,0,&observer).info.error==map::Error::ReadError,"missing index becomes a map error");
        check(lookup.cacheStats().fills==before.fills && lookup.datasetInfo().generation==generation && lookup.search(point).state==PlaceState::Ready,
              "independent map errors do not mutate the existing cache or selection");
        std::cout<<"PASS: independent map source, center/name="<<name<<", work_bytes="<<raw_bytes<<", image_bytes=6060, source_host_ms="<<elapsed<<"; 128 cache preserved\n";
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}

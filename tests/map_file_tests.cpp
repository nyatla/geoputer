#include "support/MapIndexFixture.h"
#include "map/MapFrameReceiver.h"
#include "map/DecimatedViewport.h"
#include "task/storage/galuchat/ObservedFileReader.h"
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
        map::MapFrameReceiver receiver;check(receiver.begin(request,request.buffer),"fixed UI output lent before source read");
        const auto started=std::chrono::steady_clock::now();
        auto result=readBoundaryMap(dir.string(),"ja",request,0,&observer);
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        check(result.info.error==map::Error::None && result.pixels,"independent map source succeeds");
        check(result.info.region==place.region_code,"map center matches the shared point lookup");
        check(result.plan.source.width==240 && result.plan.source.height<=101,"viewport fits the independent work limit");
        const auto after=lookup.cacheStats();
        check(after.bytes==before.bytes && after.fills==before.fills && after.hits==before.hits &&
              after.misses==before.misses && after.bounds.x==before.bounds.x && after.bounds.y==before.bounds.y &&
              lookup.datasetInfo().generation==generation && observer.rg==rg,"map never invalidates, reuses or changes the 128 cache or RG state");
        const auto raw_bytes=result.view().count*4;const auto name=std::string(place.lines[0])+" / "+place.lines[1];
        const auto plan=result.plan;
        map::MapSession session;MapBuildJob job;job.start(request,session);job.loaded(std::move(result));
        unsigned steps=0;while(!job.complete() && ++steps<20)job.step();
        check(job.complete() && job.info().error==map::Error::None,"map renders in bounded steps");
        const auto completed=job.info();
        check(job.release(request) && receiver.commit(completed) && receiver.image().bytes()==6060,"direct map output commits after writer release");
        const auto* retained=session.pixels.get();const auto retained_bytes=session.bytes();const auto before_highlight=observer.sd;
        check(session.valid && retained && retained_bytes==raw_bytes && !job.occupied(),"raster survives worker release");
        auto highlight=request;highlight.buffer={};++highlight.token;highlight.reuse=true;highlight.region_known=true;highlight.region=0;
        check(receiver.begin(highlight,highlight.buffer) && job.start(highlight,session) && !job.reading(),"highlight skips SD loading");
        while(!job.complete())job.step();
        check(session.pixels.get()==retained && observer.sd==before_highlight && job.info().region==0,"highlight reuses pixels without SD IO");
        check(job.release(highlight) && receiver.discard(highlight),"return recolor output");
        auto shifted=request;++shifted.token;shifted.longitude+=.0001;
        auto refilled=readBoundaryMap(dir.string(),"ja",shifted,0,&observer,std::move(session));
        check(refilled.info.error==map::Error::None && refilled.pixels.get()==retained && refilled.bytes()==retained_bytes,"next viewport overwrites the same allocation");
        refilled.clear();
        const auto accesses=observer.sd;
        const auto repeated=lookup.search(point);
        check(repeated.state==PlaceState::Ready && repeated.region_code==place.region_code && observer.sd==accesses && lookup.cacheStats().fills==before.fills,
              "original map and name caches still hit without SD IO after independent rendering");
        for(unsigned scale:{2u,4u}) {
            auto scaled=request;scaled.buffer={};scaled.scale=scale;++scaled.token;
            map::MapFrameReceiver sampled_frame;check(sampled_frame.begin(scaled,scaled.buffer),"sampled fixed output loan");
            auto small=readBoundaryMap(dir.string(),"ja",scaled,0,&observer);
            check(small.info.error==map::Error::None && small.pixels,"sampled map source succeeds");
            check(small.info.region==place.region_code,"sampled map retains its generation-center code");
            check(small.view().count*4==raw_bytes && small.info.meters_per_pixel==plan.meters_per_pixel*scale,
                  "sampled map uses constant code memory and a correctly scaled distance bar");
            const auto info=DatasetIndex{}.loadMap((dir/"_mapindex.dat").string(),(dir/"map.glc").string());
            DiskIndexedMapReader reader(info,std::make_shared<ObservedFileReaderFactory>((dir/"map.glc").string(),nullptr),
                                       std::make_shared<ObservedFileReaderFactory>((dir/"_mapindex.dat").string(),nullptr));
            map::DecimatedViewport viewport;
            check(map::planDecimatedViewport(scaled.latitude,scaled.longitude,reader.unitInvX(),reader.unitInvY(),SIZE_MAX,
                                             scale,viewport)==map::Error::None,"reference input plan succeeds");
            const auto& b=viewport.input;std::vector<uint32_t> full(size_t(b.width)*b.height);
            reader.readRect({b.x,b.y,b.width,b.height},full.data());
            std::vector<uint32_t> expected(small.view().count);
            for(int y=0;y<small.plan.source.height;++y)for(int x=0;x<small.plan.source.width;++x)
                expected[size_t(y)*small.plan.source.width+x]=full[size_t(y*scale)*b.width+x*scale];
            check(!std::memcmp(expected.data(),small.pixels.get(),expected.size()*4),"direct sampled result differs from full-resolution reference decimation");
            map::BoundaryImage edges;edges.resize(240,101);
            check(map::buildBoundaryRows({expected.data(),small.plan.source,expected.size()},small.plan,place.region_code,0,101,edges)==map::Error::None,
                  "reference decimated image uses the ordinary edge detector");
            map::MapSession sampled_session;MapBuildJob sampled_job;sampled_job.start(scaled,sampled_session);sampled_job.loaded(std::move(small));
            while(!sampled_job.complete())sampled_job.step();
            const auto sampled_info=sampled_job.info();
            check(sampled_job.release(scaled) && sampled_frame.commit(sampled_info) &&
                  !std::memcmp(sampled_frame.image().data(),edges.data(),edges.bytes()),
                  "direct boundary output differs from decimate-then-extract reference");
            check(lookup.cacheStats().fills==before.fills && lookup.cacheStats().hits==before.hits+1 && observer.rg==rg,
                  "sampled maps alter the independent point cache");
            std::cout<<"PASS: decimate first scale="<<scale<<", raw_input="<<b.width<<'x'<<b.height<<", work_bytes="<<expected.size()*4<<", name="<<place.lines[0]<<" / "<<place.lines[1]<<'\n';
        }
        std::ofstream(dir/"dataset.ini",std::ios::app)<<"display_line1=AREA %0 %%\ndisplay_line2=%1%2%3%4%5\n";
        lookup.configure(dir.string(),"ja");check(lookup.prepare().state==PlaceState::Ready,"formatted point lookup prepares");
        const auto formatted=lookup.search(point);auto formatted_map=readBoundaryMap(dir.string(),"ja",request,0,&observer);
        check(formatted_map.info.error==map::Error::None && std::string(formatted.lines[0]).find("AREA ")==0,"shared lookup honours formatting independently of map generation");
        std::ofstream(dir/"dataset.ini")<<"[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=Test\nwordbook=names.giswordbook\ndisplay_line1=\ndisplay_line2=\n";
        auto blank=readBoundaryMap(dir.string(),"ja",request,0,&observer);
        check(blank.info.error==map::Error::None,"map generation is independent of place row formatting");
        fs::rename(dir/"names.giswordbook",dir/"names.hidden");
        check(readBoundaryMap(dir.string(),"ja",request,0,&observer).info.error==map::Error::None,
              "boundary generation must not open or search the Wordbook");
        fs::rename(dir/"names.hidden",dir/"names.giswordbook");
        const auto configured_generation=lookup.datasetInfo().generation;
        const auto configured_stats=lookup.cacheStats();
        fs::remove(dir/"_mapindex.dat");
        check(readBoundaryMap(dir.string(),"ja",request,0,&observer).info.error==map::Error::ReadError,"missing index becomes a map error");
        check(lookup.datasetInfo().generation==configured_generation && lookup.cacheStats().fills==configured_stats.fills && lookup.search(point).state==PlaceState::Ready,
              "independent map errors do not mutate the existing cache or selection");
        std::cout<<"PASS: independent map source, center/name="<<name<<", work_bytes="<<raw_bytes<<", image_bytes=6060, source_host_ms="<<elapsed<<"; 128 cache preserved\n";
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}

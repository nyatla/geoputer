#include "performance/ScaledBoundaryPrototype.h"
#include "support/MapIndexFixture.h"
#include "task/storage/galuchat/DiskIndexedMapReader.h"
#include "task/storage/galuchat/ObservedFileReader.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
using namespace geoputer;
using namespace geoputer::benchmark;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
// Independent, unstriped oracle: evaluate high-resolution edges, then max-pool.
map::BoundaryImage reference(const ScalePlan& p,const std::vector<uint32_t>& pixels,uint32_t current) {
    map::BoundaryImage out;check(out.resize(240,101),"oracle image allocation");
    auto get=[&](unsigned x,int y){return pixels[size_t(y-p.source.y)*p.source.width+x];};
    for(unsigned y=0;y<p.high_height;++y)for(unsigned x=0;x<p.high_width;++x){
        const int row=p.sampleY(y);const auto a=get(x,row),b=x+1<p.high_width?get(x+1,row):a,c=y+1<p.high_height?get(x,p.sampleY(y+1)):a;
        if(a==b && a==c)continue;
        const uint8_t v=current && (a==current || b==current || c==current)?2:1;
        const auto dx=x/p.scale,dy=y/p.scale;out.set(dx,dy,std::max(v,out.get(dx,dy)));
    }return out;
}
void synthetic() {
    for(auto scale:{1u,2u,4u})for(auto latitude:{0.,35.681641,70.}) {
        const auto p=scalePlan(latitude,140.027194,10000,10000,scale);
        std::vector<uint32_t> data(size_t(p.source.width)*p.source.height);
        for(int y=0;y<p.source.height;++y)for(int x=0;x<p.source.width;++x)
            data[size_t(y)*p.source.width+x]=(x==137 || (y%17==0 && x%7<2))?7:x<90?0:3;
        const auto expected=reference(p,data,7);
        for(unsigned rows:{1u,3u,8u,16u}) {
            if(uint64_t(p.source.width)*(rows+1)*4>config::MapViewCodeMaxBytes)continue;
            map::BoundaryImage actual;
            const auto stats=renderStrips(p,7,rows,actual,[&](const galuchat::Rect& r,uint32_t* dst){
                for(int y=0;y<r.height;++y)std::copy_n(data.data()+size_t(r.y-p.source.y+y)*p.source.width,r.width,dst+size_t(y)*r.width);
            },[]{});
            check(stats.source_pixels==data.size(),"every raw pixel requested exactly once");
            check(!std::memcmp(actual.data(),expected.data(),actual.bytes()),"strip or scale boundary mismatch");
        }
        if(scale==1){map::BoundaryImage normal;normal.resize(240,101);check(map::buildBoundaryRows({data.data(),p.source,data.size()},p.base,7,0,101,normal)==map::Error::None,"normal extraction");check(!std::memcmp(normal.data(),expected.data(),normal.bytes()),"scale=1 differs from existing kernel");}
    }
    std::cout<<"PASS: source pixels requested once, carry halos, thin regions, 1/2 and 1/4 max-pooling and existing 1x kernel\n";
}
int main(int argc,char** argv) {
    try {
        synthetic();if(argc==1)return 0;if(argc!=4)return 2;
        namespace fs=std::filesystem;fs::path dir=argv[1];fs::create_directories(dir);fs::copy_file(argv[2],dir/"map.glc");fs::copy_file(argv[3],dir/"names.giswordbook");
        test_index::generate(dir.string(),"map.glc","names.giswordbook");
        FileIoTiming map_io,index_io;
        const auto info=test_storage::Index{}.loadMap((dir/"_mapindex.dat").string(),(dir/"map.glc").string());
        const auto mp=(dir/"map.glc").string(),ip=(dir/"_mapindex.dat").string();ReadFileSlot slot;
        FileReadSource map_source(mp,nullptr,&map_io,{4096,4096},0,&slot),index_source(ip,nullptr,&index_io,{4096,0},0,&slot);
        test_storage::MapReader reader(info,map_source,index_source);
        for(unsigned scale:{1u,2u,4u}) {
        const auto p=scalePlan(35.681641,140.027194,reader.unitInvX(),reader.unitInvY(),scale);
        std::vector<uint32_t> full(size_t(p.source.width)*p.source.height);reader.readRect({p.source.x,p.source.y,p.source.width,p.source.height},full.data());
        const auto current=full[size_t(p.base.center_y-p.source.y)*p.source.width+p.base.center_x-p.source.x];
        const auto expected=reference(p,full,current);
        for(unsigned rows:{8u,16u}){map_io={};index_io={};map::BoundaryImage actual;
            const auto stats=renderStrips(p,current,rows,actual,[&](const galuchat::Rect& r,uint32_t* dst){reader.readRect(r,dst);},[]{});
            check(!std::memcmp(actual.data(),expected.data(),actual.bytes()),"real dataset strip/full result mismatch");
            std::cout<<"PASS: real scale="<<scale<<" band="<<rows<<" source="<<p.source.width<<"x"<<p.source.height<<" work="<<stats.work_bytes<<" calls="<<stats.calls<<" map_bytes="<<map_io.bytes<<" index_bytes="<<index_io.bytes<<" hash="<<boundaryHash(actual)<<"\n";
        }
        }
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

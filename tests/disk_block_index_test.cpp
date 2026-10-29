#include "support/TracedSdFile.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "task/storage/galuchat/DiskIndexedMapReader.h"
#include "galuchat/rle_fixture.h"
#include "map/BoundaryExtractor.h"

namespace {
using Bytes = std::vector<uint8_t>;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void put(uint8_t* p, uint32_t value) { for (int i=0;i<4;++i) p[i]=uint8_t(value>>(8*i)); }
void mb(Bytes& b, size_t v) {
    if (v<252) b.push_back(uint8_t(v));
    else if (v<508) {b.push_back(255);b.push_back(uint8_t(v-252));}
    else if (v<66044) {v-=508;b.push_back(254);b.push_back(uint8_t(v>>8));b.push_back(uint8_t(v));}
    else {v-=66044;b.push_back(253);b.push_back(uint8_t(v>>16));b.push_back(uint8_t(v>>8));b.push_back(uint8_t(v));}
}
Bytes chunk(const char* tag, const Bytes& payload) {
    Bytes result(tag,tag+4);mb(result,payload.size());result.insert(result.end(),payload.begin(),payload.end());return result;
}
struct Fixture {
    Bytes map, index;
    geoputer::DatasetMapIndex info{};
    size_t first=0;
    static constexpr size_t BlockBytes=16386, PerGroup=4, Total=24;
    explicit Fixture(bool patterned=false, size_t per_group=PerGroup, unsigned side=128) {
        const size_t block_bytes=size_t(side)*side+2;
        check(per_group && Total%per_group==0 && (side==128 || side==256),"fixture grid");
        Bytes h(16);const std::string format="WGSMapSet/3";std::copy(format.begin(),format.end(),h.begin());
        h.insert(h.end(),{1,1,0,1,0,0});map=chunk("GLCH",h);
        const auto start=map.size();
        Bytes payload;mb(payload,side*6);mb(payload,side*4);payload.push_back(side==128?7:8);
        for (size_t i=0;i<Total;++i) {
            payload.push_back(0);payload.push_back(0x70);
            for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x)
                payload.push_back(patterned?uint8_t((i*19+x*3+y*5)%251):uint8_t(i+1));
        }
        const auto image=chunk("GI01",payload);map.insert(map.end(),image.begin(),image.end());
        first=map.size()-Total*block_bytes;
        const uint32_t index_bytes=40+Total/per_group*16;
        put(info.header.data()+16,64+index_bytes);put(info.header.data()+20,map.size());
        put(info.header.data()+36,1);put(info.header.data()+40,1);put(info.header.data()+44,1);put(info.header.data()+60,index_bytes);
        info.width=side*6;info.height=side*4;
        index.assign(info.header.begin(),info.header.end());
        std::array<uint8_t,40> c{};
        put(c.data()+8,side*6);put(c.data()+12,side*4);put(c.data()+16,side);
        put(c.data()+20,start);put(c.data()+24,image.size());put(c.data()+28,first);
        put(c.data()+32,Total);put(c.data()+36,Total/per_group);index.insert(index.end(),c.begin(),c.end());
        for (size_t n=0;n<Total;n+=per_group) {
            std::array<uint8_t,16> g{};put(g.data(),n);put(g.data()+4,per_group);
            put(g.data()+8,first+n*block_bytes);put(g.data()+12,per_group*block_bytes);
            index.insert(index.end(),g.begin(),g.end());
        }
    }
};
using Trace=traced_sd::Trace;
using SdFile=traced_sd::File;
void rectangles() {
    Fixture f;
    for (const galuchat::Rect target : {galuchat::Rect{256,0,1,256},{520,390,128,128},{0,128,768,256},
                                       {125,125,260,260},{-20,-20,128,128},{760,500,128,128},{800,600,1,1}}) {
        Trace map_trace,index_trace;map_trace.peer=&index_trace;index_trace.peer=&map_trace;
        auto map=std::make_shared<SdFile>(f.map,map_trace),index=std::make_shared<SdFile>(f.index,index_trace);
        test_storage::MapReader reader(f.info,*map,*index);
        std::vector<uint32_t> pixels(size_t(target.width)*target.height,999);
        reader.readRect(target,pixels.data());
        std::array<bool,6> needed{};
        for (int y=0;y<target.height;++y) for (int x=0;x<target.width;++x) {
            const int px=target.x+x,py=target.y+y;
            uint32_t expected=0;
            if (px>=0 && px<768 && py>=0 && py<512) {
                const auto block=size_t(py/128)*6+px/128;expected=uint32_t(block+1);needed[block/4]=true;
            }
            check(pixels[size_t(y)*target.width+x]==expected,"grouped rectangle pixels differ");
        }
        const auto first=std::find(needed.begin(),needed.end(),true);
        const bool hit=first!=needed.end();
        check(index_trace.opens.size()==1,"index should open only once");
        check(map_trace.opens.size()==size_t(hit),"one GLC handle should serve all selected groups");
        if (hit) check(map_trace.opens[0]==f.first+size_t(first-needed.begin())*4*Fixture::BlockBytes,"GLC must open directly at the first selected group");
        for (auto offset:map_trace.reads) {
            check(offset>=f.first,"GI01 prefix should not be read on the indexed path");
            check(needed[(offset-f.first)/(4*Fixture::BlockBytes)],"decoder read an unselected group");
        }
    }
}
void decimation() {
    Fixture f(true);
    for (unsigned scale : {1u,2u,4u})
    for (const galuchat::Rect rect : {galuchat::Rect{0,0,768,512},{1,3,259,133},{-7,-5,137,139},
                                     {123,127,267,261},{757,503,17,13},{800,600,1,1},{0,0,1,1}}) {
        Trace mt,it;mt.peer=&it;it.peer=&mt;
        SdFile map_source(f.map,mt),index_source(f.index,it);
        test_storage::MapReader reader(f.info,map_source,index_source);
        const unsigned width=1+(unsigned(rect.width)-1)/scale,height=1+(unsigned(rect.height)-1)/scale;
        std::vector<uint32_t> pixels(size_t(width)*height+2,0xdeadbeef);
        reader.readRectDecimated(rect,scale,pixels.data()+1);
        check(pixels.front()==0xdeadbeef && pixels.back()==0xdeadbeef,"decimation overran output capacity");
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const int px=rect.x+int(x*scale),py=rect.y+int(y*scale);
            uint32_t expected=0;
            if(px>=0 && py>=0 && px<768 && py<512)
                expected=uint32_t((size_t(py/128)*6+px/128)*19+(px%128)*3+(py%128)*5)%251;
            check(pixels[1+size_t(y)*width+x]==expected,"decimation differs from independently sampled source");
        }
        check(it.opens.size()==1 && mt.opens.size()<=1,"decimation repeats index/map sessions");
    }
    Trace mt,it;mt.peer=&it;it.peer=&mt;
    SdFile map_source(f.map,mt),index_source(f.index,it);
    test_storage::MapReader reader(f.info,map_source,index_source);
    for(unsigned scale:{0u,3u,8u}) {
        uint32_t pixel=123;bool rejected=false;
        try {reader.readRectDecimated({0,0,1,1},scale,&pixel);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected && pixel==123 && mt.opens.empty() && it.opens.empty(),"invalid scale started reading or changed output");
    }
    for(auto rect:{galuchat::Rect{0,0,0,1},galuchat::Rect{0,0,1,-1}}) {
        uint32_t pixel=123;bool rejected=false;
        try {reader.readRectDecimated(rect,4,&pixel);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected && pixel==123,"invalid sampled dimensions accepted");
    }
}
void decimationBeforeEdges() {
    Fixture f;
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)f.map[f.first+2+y*128+x]=7;
    f.map[f.first+2+128+1]=99; // One-cell island between the scale-2 samples.
    auto mf=std::make_shared<traced_sd::File>(f.map),ix=std::make_shared<traced_sd::File>(f.index);
    test_storage::MapReader reader(f.info,*mf,*ix);
    uint32_t full[64]{},small[16]{};reader.readRect({0,0,8,8},full);
    check(full[9]==99 && full[0]==7,"fine-island fixture has no island");
    reader.readRectDecimated({0,0,8,8},2,small);
    geoputer::map::ViewportPlan plan;plan.source={0,0,4,4};plan.width=4;plan.height=4;
    plan.center_x=2;plan.center_y=1;plan.grid_rows_per_pixel=1;
    geoputer::map::BoundaryImage edges;edges.resize(4,4);
    check(geoputer::map::buildBoundaryRows({small,{0,0,4,4},16},plan,7,0,4,edges)==geoputer::map::Error::None,
          "ordinary edge detector rejected the decimated image");
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
        check(small[y*4+x]==7 && edges.get(x,y)==0,"fine boundary was preserved instead of decimating codes first");
}
void failuresAndBypass() {
    Fixture f;
    // A corrupt skipped prefix proves that decoding actually restarts at group 2.
    f.map[f.first]=0x30;
    Trace mt,it;mt.peer=&it;it.peer=&mt;auto map=std::make_shared<SdFile>(f.map,mt),index=std::make_shared<SdFile>(f.index,it);
    test_storage::MapReader reader(f.info,*map,*index);uint32_t pixel=0;
    reader.readRect({256,128,1,1},&pixel);check(pixel==9,"indexed group could not bypass an unused corrupt prefix");
    check(mt.opens==std::vector<size_t>{f.first+8*Fixture::BlockBytes},"incorrect restarted file offset");
    const auto good=f.index;
    for (const auto field : {size_t(104),size_t(108),size_t(112),size_t(116),size_t(184+12)}) {
        f.index=good;put(f.index.data()+field,0xffffffff);
        bool failed=false;try {reader.readRect({256,128,1,1},&pixel);}catch(const std::runtime_error&){failed=true;}
        check(failed,"malformed runtime group metadata accepted");
    }
    f.index=good;f.map[f.first+8*Fixture::BlockBytes]=0x30;
    bool failed=false;try {reader.readRect({256,128,1,1},&pixel);}catch(const std::runtime_error&){failed=true;}
    check(failed,"corruption inside a selected group accepted");
}
void boundedSelection() {
    // A 1/4 viewport straddling both axes selects exactly 5x3 256-cell blocks.
    // One index group per block makes this the worst case for the 15 slots.
    Fixture f(false,1,256);
    for(const galuchat::Rect rect:{galuchat::Rect{255,255,957,329}, {0,0,1536,1024}}) {
        Trace mt,it;mt.peer=&it;it.peer=&mt;
        auto mf=std::make_shared<SdFile>(f.map,mt),ix=std::make_shared<SdFile>(f.index,it);
        test_storage::MapReader reader(f.info,*mf,*ix);
        const unsigned width=1+(unsigned(rect.width)-1)/4,height=1+(unsigned(rect.height)-1)/4;
        std::vector<uint32_t> pixels(size_t(width)*height+2,0xdeadbeef);
        reader.readRectDecimated(rect,4,pixels.data()+1);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const unsigned px=unsigned(rect.x)+4*x,py=unsigned(rect.y)+4*y;
            check(pixels[1+size_t(y)*width+x]==(py/256)*6+px/256+1,"bounded group batch lost pixels");
        }
        check(pixels.front()==0xdeadbeef && pixels.back()==0xdeadbeef,"group cache overflow damaged output");
        const size_t batches=rect.x?1:2;
        check(mt.opens.size()==batches && it.opens.size()==batches,"wrong number of bounded batches");
        check(!mt.live && !it.live && mt.peak==1 && it.peak==1,"Reader lifetime escaped read");
    }
    // Failures release both Readers, including failure on the next full-index pass.
    for(bool source_failure:{false,true}) {
        Trace mt,it;mt.peer=&it;it.peer=&mt;
        if(source_failure)mt.fail_at=f.first+2;else it.fail_at=104+16*16;
        SdFile map_source(f.map,mt),index_source(f.index,it);
        test_storage::MapReader reader(f.info,map_source,index_source);
        std::vector<uint32_t> pixels(size_t(1536)*1024);
        bool failed=false;
        try{reader.readRect({0,0,1536,1024},pixels.data());}catch(const std::runtime_error&){failed=true;}
        check(failed && !mt.live && !it.live,"failure leaked a suspended Reader");
    }
}

void wholeRequestSelection() {
    // Two adjacent 256-cell grids contribute 6+9 references. A trailing,
    // nonintersecting chunk proves the index is fully scanned before GLC I/O.
    Bytes map=chunk("GLCH",Bytes(16)), records;
    for(unsigned c=0;c<3;++c) {
        const unsigned columns=c==0?2:c==1?3:1,rows=c<2?3:1;
        const unsigned west=c==0?0:c==1?512:10000,south=c<2?0:10000;
        std::vector<Bytes> blocks;
        for(unsigned b=0;b<columns*rows;++b) {
            Bytes raw{0x70};raw.insert(raw.end(),256*256,uint8_t(c*20+b+1));
            blocks.push_back(rle_fixture::block(0,raw));
        }
        const auto image=rle_fixture::image(256,blocks,columns);
        const auto start=map.size();rle_fixture::append(map,image);
        size_t first=map.size();for(const auto& b:blocks)first-=b.size();
        std::array<uint8_t,40> record{};
        put(record.data(),west);put(record.data()+4,south);put(record.data()+8,columns*256);put(record.data()+12,rows*256);
        put(record.data()+16,256);put(record.data()+20,start);put(record.data()+24,image.size());put(record.data()+28,first);
        put(record.data()+32,blocks.size());put(record.data()+36,blocks.size());
        rle_fixture::append(records,Bytes(record.begin(),record.end()));
        for(size_t b=0,offset=first;b<blocks.size();offset+=blocks[b++].size()) {
            std::array<uint8_t,16> g{};put(g.data(),b);put(g.data()+4,1);put(g.data()+8,offset);put(g.data()+12,blocks[b].size());
            rle_fixture::append(records,Bytes(g.begin(),g.end()));
        }
    }
    geoputer::DatasetMapIndex info{};put(info.header.data()+16,64+records.size());put(info.header.data()+20,map.size());
    put(info.header.data()+36,3);put(info.header.data()+60,records.size());
    Bytes index(info.header.begin(),info.header.end());rle_fixture::append(index,records);
    Trace mt,it;mt.peer=&it;it.peer=&mt;
    SdFile map_source(map,mt),index_source(index,it);
    test_storage::MapReader reader(info,map_source,index_source);
    constexpr unsigned width=240,height=83;
    std::vector<uint32_t> pixels(width*height);
    reader.readRectDecimated({255,255,957,329},4,pixels.data());
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        const unsigned px=255+4*x,py=255+4*y;
        const auto expected=px<512?(py/256)*2+px/256+1:21+(py/256)*3+(px-512)/256;
        check(pixels[y*width+x]==expected,"whole-request selection lost a chunk's geometry");
    }
    check(it.opens.size()==1 && mt.opens.size()==1 && !it.live && !mt.live,"whole request reopened index/GLC");
    // A malformed trailing chunk must be rejected before any GLC is opened.
    put(index.data()+64+40+6*16+40+9*16+8,0);
    mt={};it={};mt.peer=&it;it.peer=&mt;
    bool failed=false;
    try{reader.readRectDecimated({255,255,957,329},4,pixels.data());}catch(const std::runtime_error&){failed=true;}
    check(failed && mt.opens.empty() && !it.live,"GLC opened before the entire index was checked");
}

void rleComposition() {
    // Exercise the actual application's uint32 destination, both ungrouped GI01s
    // and disk-indexed block groups, with transparent zeros and overlapping chunks.
    for (bool grouped : {false,true}) for (int mode=0;mode<4;++mode) {
        const int r=grouped?128:16, columns=grouped?2:1;
        for (int invalid : {0,1,2}) for(int kind:{0,1,2}) for(int compression:{0,1,2}) {
            Bytes h(16); const std::string format="WGSMapSet/3";
            std::copy(format.begin(),format.end(),h.begin());
            h.insert(h.end(),{1,1,0,2,0,0,0,0});
            Bytes map=chunk("GLCH",h), records;
            std::vector<Bytes> images;
            for (int layer=0;layer<2;++layer) {
                const uint64_t value=invalid && layer==1 ? (invalid==1?uint64_t(UINT32_MAX)+1:0x81) : layer?99:kind==1?11:0;
                Bytes cell;
                if(!kind)cell=rle_fixture::cell(mode,0,rle_fixture::runs(r,2),value);
                else {cell={0x28};rle_fixture::append(cell,rle_fixture::mb(value));}
                std::vector<Bytes> blocks{rle_fixture::block(compression,cell)};
                // Delta palette encoding turns byte 0x81 into -1.
                if(invalid==2 && layer==1) blocks[0][0]|=0x80;
                if(grouped) {Bytes raw{0x70};raw.insert(raw.end(),size_t(r)*r,layer?77:0);blocks.push_back(rle_fixture::block(compression,raw));}
                const auto image=rle_fixture::image(r,blocks,columns);images.push_back(image);
                const auto start=map.size();rle_fixture::append(map,image);
                size_t first=map.size();for(const auto& b:blocks)first-=b.size();
                std::array<uint8_t,40> c{};
                put(c.data()+8,columns*r);put(c.data()+12,r);put(c.data()+16,r);
                put(c.data()+20,start);put(c.data()+24,image.size());put(c.data()+28,first);
                put(c.data()+32,blocks.size());put(c.data()+36,grouped?blocks.size():0);
                records.insert(records.end(),c.begin(),c.end());
                if(grouped)for(size_t i=0,offset=first;i<blocks.size();offset+=blocks[i++].size()) {
                    std::array<uint8_t,16> g{};put(g.data(),i);put(g.data()+4,1);
                    put(g.data()+8,offset);put(g.data()+12,blocks[i].size());
                    records.insert(records.end(),g.begin(),g.end());
                }
            }
            geoputer::DatasetMapIndex info{};
            put(info.header.data()+16,64+records.size());put(info.header.data()+20,map.size());
            put(info.header.data()+36,2);put(info.header.data()+40,1);put(info.header.data()+44,1);put(info.header.data()+60,records.size());
            Bytes index(info.header.begin(),info.header.end());rle_fixture::append(index,records);
            auto mf=std::make_shared<traced_sd::File>(map);
            auto ix=std::make_shared<traced_sd::File>(index);
            test_storage::MapReader actual(info,*mf,*ix);
            std::vector<galuchat::RawRaster> layers;
            if(!invalid)for(const auto& bytes:images) {
                galuchat::BufferReader source(bytes);galuchat::GaluchatImageDataChunk01Reader chunk(source);
                layers.push_back(chunk.toRaster()); // Legacy set-only decoding of each layer.
            }
            for(auto rect:std::vector<galuchat::Rect>{{0,0,columns*r,r},{-2,-3,r+5,r+6},{r/2,r/2,3,2}}) {
                std::vector<uint32_t> pixels(size_t(rect.width)*rect.height,999);
                if(invalid) {
                    if(rect.x || rect.y) continue; // Include the deliberately invalid run.
                    bool failed=false;
                    try {actual.readRect(rect,pixels.data());}catch(const std::runtime_error& e){failed=std::string(e.what())=="region code exceeds uint32 cache range";}
                    check(failed,"RLE/MONO uint32 range error accepted");
                    failed=false;
                    try {actual.readRectDecimated(rect,4,pixels.data());}catch(const std::runtime_error& e){failed=std::string(e.what())=="region code exceeds uint32 cache range";}
                    check(failed,"sampled destination suppressed invalid region codes");
                } else {
                    actual.readRect(rect,pixels.data());
                    for(unsigned scale:{1u,2u,4u}) {
                        const unsigned sw=1+(unsigned(rect.width)-1)/scale,sh=1+(unsigned(rect.height)-1)/scale;
                        std::vector<uint32_t> sampled(size_t(sw)*sh+2,0xdeadbeef);
                        actual.readRectDecimated(rect,scale,sampled.data()+1);
                        check(sampled.front()==0xdeadbeef && sampled.back()==0xdeadbeef,"sampled RLE/MONO overran output");
                        for(unsigned sy=0;sy<sh;++sy)for(unsigned sx=0;sx<sw;++sx)
                            check(sampled[1+size_t(sy)*sw+sx]==pixels[size_t(sy*scale)*rect.width+sx*scale],
                                  "sampled RLE/MONO/RAW composition or reverse span differs");
                    }
                    for(int y=0;y<rect.height;++y)for(int x=0;x<rect.width;++x) {
                        const int px=rect.x+x,py=rect.y+y;int64_t expected=0;
                        if(px>=0 && px<columns*r && py>=0 && py<r) {
                            const size_t offset=size_t(py)*columns*r+px;
                            // The app preserves the first nonzero layer. The SDK's
                            // full-map rectangle wrapper instead permits overwrites.
                            expected=layers[0].data[offset];if(!expected)expected=layers[1].data[offset];
                        }
                        check(pixels[size_t(y)*rect.width+x]==expected,"RLE/MONO destination composition differs");
                    }
                }
            }
        }
    }
}

}
int main() {
    rectangles();decimation();decimationBeforeEdges();failuresAndBypass();boundedSelection();wholeRequestSelection();rleComposition();
    std::cout<<"disk block index: pixels, group/row boundaries, direct offsets, exclusive Readers, whole-request 15-group cache, batching and failures OK\n";
}

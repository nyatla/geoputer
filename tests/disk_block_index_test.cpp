#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "task/storage/galuchat/DiskIndexedMapReader.h"

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
    Fixture() {
        Bytes h(16);const std::string format="WGSMapSet/3";std::copy(format.begin(),format.end(),h.begin());
        h.insert(h.end(),{1,1,0,1,0,0});map=chunk("GLCH",h);
        const auto start=map.size();
        Bytes payload;mb(payload,768);mb(payload,512);payload.push_back(7);
        for (size_t i=0;i<Total;++i) {payload.push_back(0);payload.push_back(0x70);payload.insert(payload.end(),16384,uint8_t(i+1));}
        const auto image=chunk("GI01",payload);map.insert(map.end(),image.begin(),image.end());
        first=map.size()-Total*BlockBytes;
        const uint32_t index_bytes=40+Total/PerGroup*16;
        put(info.header.data()+16,64+index_bytes);put(info.header.data()+20,map.size());
        put(info.header.data()+36,1);put(info.header.data()+40,1);put(info.header.data()+44,1);put(info.header.data()+60,index_bytes);
        info.width=768;info.height=512;
        index.assign(info.header.begin(),info.header.end());
        std::array<uint8_t,40> c{};
        put(c.data()+8,768);put(c.data()+12,512);put(c.data()+16,128);
        put(c.data()+20,start);put(c.data()+24,image.size());put(c.data()+28,first);
        put(c.data()+32,Total);put(c.data()+36,Total/PerGroup);index.insert(index.end(),c.begin(),c.end());
        for (size_t n=0;n<Total;n+=PerGroup) {
            std::array<uint8_t,16> g{};put(g.data(),n);put(g.data()+4,PerGroup);
            put(g.data()+8,first+n*BlockBytes);put(g.data()+12,PerGroup*BlockBytes);
            index.insert(index.end(),g.begin(),g.end());
        }
    }
};
struct Trace {std::vector<size_t> opens, reads;};
class Factory final : public galuchat::ReaderFactory {
public:
    Factory(const Bytes& bytes, Trace& trace):bytes_(bytes),trace_(trace) {}
    std::unique_ptr<galuchat::ByteReader> create(size_t offset=0) const override {return createRange(offset,bytes_.size()-offset);}
    std::unique_ptr<galuchat::ByteReader> createRange(size_t offset,size_t length) const override {
        trace_.opens.push_back(offset);
        class Reader final : public galuchat::ByteReader {
        public:
            Reader(std::unique_ptr<galuchat::ByteReader> input,size_t base,Trace& trace):input_(std::move(input)),base_(base),trace_(trace) {}
            size_t pos() const override {return input_->pos();}
            bool atEnd() const override {return input_->atEnd();}
        protected:
            uint8_t nextByte() override {trace_.reads.push_back(base_+pos());return input_->readByte();}
            void skipByte(size_t n) override {input_->skipInByte(n);}
        private:
            std::unique_ptr<galuchat::ByteReader> input_;size_t base_;Trace& trace_;
        };
        return std::make_unique<Reader>(galuchat::BufferReaderFactory(bytes_).createRange(offset,length),offset,trace_);
    }
private:const Bytes& bytes_;Trace& trace_;
};
void rectangles() {
    Fixture f;
    for (const galuchat::Rect target : {galuchat::Rect{256,0,1,256},{520,390,128,128},{0,128,768,256},
                                       {125,125,260,260},{-20,-20,128,128},{760,500,128,128},{800,600,1,1}}) {
        Trace map_trace,index_trace;
        auto map=std::make_shared<Factory>(f.map,map_trace),index=std::make_shared<Factory>(f.index,index_trace);
        geoputer::DiskIndexedMapReader reader(f.info,map,index);
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
void failuresAndBypass() {
    Fixture f;
    // A corrupt skipped prefix proves that decoding actually restarts at group 2.
    f.map[f.first]=0x30;
    Trace mt,it;auto map=std::make_shared<Factory>(f.map,mt),index=std::make_shared<Factory>(f.index,it);
    geoputer::DiskIndexedMapReader reader(f.info,map,index);uint32_t pixel=0;
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
}
int main() {
    rectangles();failuresAndBypass();
    std::cout<<"disk block index: pixels, group/row boundaries, direct offsets, single open and failures OK\n";
}

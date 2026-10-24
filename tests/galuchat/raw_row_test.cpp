#include <chrono>
#include <atomic>
#include <iomanip>
#include <iostream>
#include <string>
#include <galuchat/wgsmap3_reader.hpp>
#include "raw_fixture.h"
namespace {
using namespace raw_fixture;
uint64_t digest=14695981039346656037ull;
void mix(uint64_t value) { for(int i=0;i<8;++i) {digest^=value&255;digest*=1099511628211ull;value>>=8;} }
struct Reader : galuchat::ByteReader {
    const Bytes& bytes;size_t at=0;
    explicit Reader(const Bytes& b):bytes(b) {}
    size_t pos() const override {mix('P');mix(at);return at;}
    bool atEnd() const override {mix('E');mix(at);return at==bytes.size();}
    uint8_t nextByte() override {mix('R');mix(at);if(at==bytes.size())throw std::runtime_error("fixture EOF");return bytes[at++];}
    void skipByte(size_t n) override {mix('S');mix(at);mix(n);if(n>bytes.size()-at)throw std::runtime_error("fixture EOF");at+=n;}
};
struct Raster {
    int width,height;std::vector<int64_t> data;bool fail=false;
    Raster(int w,int h,bool f=false):width(w),height(h),data(size_t(w+3)*(h+2),-1),fail(f) {}
    void set(int x,int y,int64_t value) {
        require(x>=0 && x<width && y>=0 && y<height,"RAW write outside destination");
        mix('W');mix(x);mix(y);mix(uint64_t(value));
        if(fail && x==width/2 && y==height/2)throw std::runtime_error("destination failure");
        data[size_t(y)*(width+3)+x]=value;
    }
};
void trace(const Bytes& bytes,const galuchat::Rect& rect,bool tail,bool fail=false) {
    Reader source(bytes);Raster dest(rect.width,rect.height,fail);
    try {
        galuchat::GaluchatImageDataChunk01Reader reader(source);
        if(tail)reader.readRect(rect.x,rect.y,dest);else reader.readRectWithoutTail(rect.x,rect.y,dest);
        mix('V');
    } catch(const std::runtime_error& error) {mix('!');for(auto c:std::string(error.what()))mix(static_cast<unsigned char>(c));}
    mix(source.pos());mix(source.bitOffset());for(auto v:dest.data)mix(uint64_t(v));
}
void rectangles() {
    for(int r:{1,2,4,16,128})for(int mode=0;mode<4;++mode)for(int compression=0;compression<3;++compression)
    for(bool delta:{false,true}) {
        if(mode==3 && delta)continue;
        std::vector<Bytes> blocks;
        for(int b=0;b<4;++b)blocks.push_back(rawBlock(r,mode,b,compression,delta));
        const auto bytes=image(r,blocks,2);
        for(auto rect:std::vector<galuchat::Rect>{{0,0,2*r,2*r},{-2,-1,r+3,r+2},{r-1,r-1,3,3},
            {r/2,r/2,1,1},{r-1,0,1,2*r},{0,r-1,2*r,1},{2*r-1,2*r-1,2,2},{2*r,2*r,1,1},{0,0,0,0}}) {
            for(bool tail:{false,true})trace(bytes,rect,tail);
            galuchat::BufferReader source(bytes);galuchat::GaluchatImageDataChunk01Reader reader(source);
            Raster dest(rect.width,rect.height);reader.readRect(rect.x,rect.y,dest);
            for(int y=0;y<rect.height+2;++y)for(int x=0;x<rect.width+3;++x) {
                const int px=rect.x+x,py=rect.y+y;int64_t expected=-1;
                if(x<rect.width && y<rect.height && px>=0 && py>=0 && px<2*r && py<2*r) {
                    const int seed=(py/r)*2+px/r,count=paletteSize(mode);
                    expected=value(pixelIndex(r,px%r,py%r,seed,count?count:10),delta);
                }
                require(dest.data[size_t(y)*(rect.width+3)+x]==expected,"RAW independent pixel/padding oracle differs");
            }
        }
        // Start at an externally indexed block and write at its nonzero position.
        galuchat::BufferReader range(blocks[3]);Raster dest(2*r+1,2*r+1);
        galuchat::GaluchatImageDataChunk01Reader::readRectFromBlockRange(range,{2*r,2*r,r,3,1,blocks[3].size()},-1,-1,dest);
        require(range.atEnd() && !range.bitOffset(),"RAW indexed range consumption differs");
        for(int y=0;y<dest.height;++y)for(int x=0;x<dest.width;++x) {
            const int count=paletteSize(mode);
            const auto expected=x>r && y>r?value(pixelIndex(r,x-r-1,y-r-1,3,count?count:10),delta):int64_t(-1);
            require(dest.data[size_t(y)*(dest.width+3)+x]==expected,"RAW indexed destination offset differs");
        }
    }
}
void nestedAndErrors() {
    // Four one-pixel RAW children share the same palette and bit stream.
    Bits node;node.bytes({0x0e,0xff,11,22,33});
    const int modes[]{0,1,2,3};
    for(int mode:modes) {
        node.put(0x40|(mode<<4),8);if(mode==2)node.put(0,12);
        if(mode==3)node.bytes(mb(508));else node.put(mode==0?1:2,mode==0?1:mode==1?2:4);
    }
    for(int compression=0;compression<3;++compression) {
        const auto bytes=image(2,{block(compression,node.data)});
        for(int y=-1;y<=2;++y)for(int x=-1;x<=2;++x)trace(bytes,{x,y,2,2},true);
        galuchat::BufferReader source(bytes);galuchat::GaluchatImageDataChunk01Reader reader(source);
        auto result=reader.toRaster();require(result.data==std::vector<int64_t>{22,33,33,508},"RAW shared-palette/non-byte child oracle differs");
    }
    for(int mode=0;mode<4;++mode)for(int compression=0;compression<3;++compression) {
        auto cell=encodedRaw(4,mode).data;
        const auto valid=image(4,{block(compression,cell)});
        trace(valid,{0,0,4,4},true,true);
        trace(valid,{1,1,2,2},true,true);
        for(size_t cut=0;cut<valid.size();++cut)trace(Bytes(valid.begin(),valid.begin()+cut),{1,1,1,1},true);
        cell.push_back(0x81);trace(image(4,{block(compression,cell)}),{1,1,1,1},true);
        if(mode==0 || mode==3) {
            cell=encodedRaw(4,mode).data;cell[0]|=mode==0?4:1;
            trace(image(4,{block(compression,cell)}),{0,0,1,1},true);
        }
    }
    // Invalid MBUInt after the requested point must still be read and rejected.
    trace(image(2,{block(0,{0x70,1,2,3,0xfc})}),{0,0,1,1},true);
}
void benchmark(int iterations) {
    constexpr int r=128;
    for(int mode=0;mode<4;++mode)for(int compression=0;compression<3;++compression)
    for(auto profile:{0,1,2,3}) {
        const auto bytes=image(r,{rawBlock(r,mode,0,compression)});
        const galuchat::Rect target=profile==0?galuchat::Rect{0,0,r,r}:profile==1?galuchat::Rect{43,37,64,64}:
            profile==2?galuchat::Rect{63,0,1,r}:galuchat::Rect{63,63,1,1};
        galuchat::RawRaster dest(target.width,target.height);
        uint64_t sum=0;const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<iterations;++i) {
            galuchat::BufferReader source(bytes);galuchat::GaluchatImageDataChunk01Reader reader(source);
            reader.readRect(target.x,target.y,dest);sum+=uint64_t(dest.data[0]);
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        uint64_t checksum=14695981039346656037ull;
        for(auto value:dest.data) {checksum^=uint64_t(value);checksum*=1099511628211ull;}
        std::cout<<"{\"palette\":"<<mode<<",\"compression\":"<<compression<<",\"profile\":"<<profile
                 <<",\"iterations\":"<<iterations<<",\"ns\":"<<ns<<",\"sum\":"<<sum<<",\"checksum\":"<<checksum<<"}\n";
    }
}
}
int main(int argc,char** argv) {
    if(argc==2) {benchmark(std::stoi(argv[1]));return 0;}
    rectangles();nestedAndErrors();
    require(digest==UINT64_C(9130978659112753844),"RAW input/output/error trace differs from scalar baseline");
    std::cout<<"RAW rows: palette/direct, RAW/RAWS/LZSS, clipping, padding, offsets, shared bits and errors; trace="<<digest<<'\n';
}

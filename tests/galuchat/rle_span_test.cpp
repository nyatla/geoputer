#include <iostream>
#include <string>
#include "galuchat/wgsmap3_reader.hpp"
#include "rle_fixture.h"
namespace {
using namespace rle_fixture;
struct Scalar {
    int width, height;
    std::vector<int64_t> data;
    size_t writes = 0;
    Scalar(int w, int h) : width(w), height(h), data(size_t(w) * h, -1) {}
    void set(int x, int y, int64_t v) {
        require(x >= 0 && x < width && y >= 0 && y < height, "write outside destination");
        data[size_t(y) * width + x] = v; ++writes;
    }
};
struct Spans : Scalar {
    using Scalar::Scalar;
    size_t spans = 0;
    void setSpan(int x, int y, int count, int dx, int dy, int64_t v) {
        require(count > 0 && std::abs(dx) + std::abs(dy) == 1, "invalid span");
        ++spans;
        for (int i = 0; i < count; ++i) Scalar::set(x + i * dx, y + i * dy, v);
    }
    void set(int, int, int64_t) { throw std::runtime_error("RLE used scalar destination"); }
};
static_assert(!galuchat::detail::HasRleSpan<Scalar>::value);
static_assert(galuchat::detail::HasRleSpan<Spans>::value);
void compare(const Bytes& bytes, int x, int y, int w, int h, bool tail) {
    galuchat::BufferReader a(bytes), b(bytes);
    galuchat::GaluchatImageDataChunk01Reader old(a), fast(b);
    Scalar scalar(w,h); Spans span(w,h);
    if (tail) { old.readRect(x,y,scalar); fast.readRect(x,y,span); }
    else { old.readRectWithoutTail(x,y,scalar); fast.readRectWithoutTail(x,y,span); }
    require(scalar.data == span.data && scalar.writes == span.writes, "scalar/span pixels differ");
    require(a.pos() == b.pos() && a.bitOffset() == b.bitOffset(), "stream consumption differs");
}
void rectangles() {
    for (int r : {1,2,4,16,128}) for (int mode=0;mode<4;++mode)
    for (int palette=0;palette<4;++palette) for (int pattern=0;pattern<3;++pattern) {
        const auto lengths=runs(r,pattern);
        const auto oracle=expected(r,mode,palette,lengths);
        for (int compression=0;compression<3;++compression) {
            const auto part=block(compression,cell(mode,palette,lengths));
            const auto bytes=image(r,{part,part,part,part},2);
            for (auto rect : std::vector<galuchat::Rect>{{0,0,2*r,2*r},{-2,-1,r+3,r+2},
                {r-1,r-1,3,3},{r/2,r/2,1,r},{r-1,0,1,2*r},{0,r-1,2*r,1},
                {2*r,2*r,1,1},{0,0,0,0}}) {
                compare(bytes,rect.x,rect.y,rect.width,rect.height,false);
                compare(bytes,rect.x,rect.y,rect.width,rect.height,true);
            }
            galuchat::BufferReader source(bytes);
            galuchat::GaluchatImageDataChunk01Reader reader(source);
            Spans actual(2*r,2*r); reader.readRect(0,0,actual);
            for (int y=0;y<2*r;++y) for (int x=0;x<2*r;++x)
                require(actual.data[size_t(y)*2*r+x] == oracle[size_t(y%r)*r+x%r], "independent traversal oracle differs");
            // Exercise externally indexed block restart and destination offsets.
            galuchat::BufferReader range(part);
            Spans ranged(2*r,2*r);
            galuchat::GaluchatImageDataChunk01Reader::readRectFromBlockRange(
                range,{2*r,2*r,r,3,1,part.size()},0,0,ranged);
            require(range.atEnd() && !range.bitOffset(), "range stream end");
            for (int y=0;y<2*r;++y) for (int x=0;x<2*r;++x)
                require(ranged.data[size_t(y)*2*r+x] == (x>=r && y>=r ? oracle[size_t(y-r)*r+x-r] : -1), "range placement differs");
        }
    }
}
void nestedAndFailures() {
    Bits packed; packed.bytes({0,255}); // Child RLE cells share bit boundaries.
    for (int mode=0;mode<4;++mode) {
        const auto child=encodedCell(mode,mode,runs(4,2));
        for(size_t i=0;i<child.count;++i) packed.put((child.data[i/8]>>(7-i%8))&1,1);
    }
    const auto nested=packed.data;
    for (int compression=0;compression<3;++compression) {
        auto bytes=image(8,{block(compression,nested)});
        for (int x=-1;x<=8;++x) for (int y=-1;y<=8;++y) compare(bytes,x,y,3,3,true);
    }
    for (int mode=0;mode<4;++mode) for (int compression=0;compression<3;++compression) {
        std::vector<Bytes> invalid{cell(mode,0,{0}),cell(mode,0,{17}),cell(mode,0,{3})};
        if (compression) { auto extra=cell(mode,0,{16});extra.push_back(0);invalid.push_back(extra); }
        for (const auto& decoded:invalid) {
            const auto bytes=image(4,{block(compression,decoded)});
            for (bool fast : {false,true}) {
                galuchat::BufferReader source(bytes);
                galuchat::GaluchatImageDataChunk01Reader reader(source);
                Scalar scalar(1,1);Spans span(1,1);bool failed=false;
                try { if(fast)reader.readRect(0,0,span);else reader.readRect(0,0,scalar); }
                catch(const std::exception&) {failed=true;}
                require(failed,"invalid RLE accepted");
            }
        }
    }
    // A constant 128x128 run uses 128 row spans rather than 16384 set calls.
    auto bytes=image(128,{block(0,cell(0,0,{16384}))});
    galuchat::BufferReader source(bytes);galuchat::GaluchatImageDataChunk01Reader reader(source);
    Spans raster(128,128);reader.readRect(0,0,raster);
    require(raster.spans==128 && raster.writes==16384,"long-run span reduction");
}
}
int main() {
    rectangles();nestedAndFailures();
    std::cout << "RLE spans: four modes, four palettes, RAW/RAWS/LZSS, clipping, nesting, ranges and errors OK\n";
}

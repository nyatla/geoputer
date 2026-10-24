#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>
#include <galuchat/wgsmap3_reader.hpp>
#include "task/storage/galuchat/ReadTiming.h"
namespace geoputer::benchmark {
namespace raw_cell {
constexpr int Side=128;
inline void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
inline uint32_t pixel(int kind,int x,int y) {
    if(kind<3)return uint32_t(((x*7+y*3+Side)% (kind==0?2:kind==1?4:16))*11);
    if(kind==3)return uint32_t((x*7+y*3)%128);
    constexpr uint32_t values[]{0,251,252,507,508,66043,66044,16843259,16843260,UINT32_MAX};
    return values[(x*7+y*3+Side)%10];
}
struct Writer {
    std::vector<uint8_t>& bytes;
    unsigned bits=0;
    void put(uint32_t value,unsigned width) {
        for(int i=int(width)-1;i>=0;--i) {
            if(!bits)bytes.push_back(0);
            bytes.back()|=((value>>i)&1)<<(7-bits);
            bits=(bits+1)%8;
        }
    }
    static unsigned mbBytes(uint32_t n) {return n<252?1:n<508?2:n<66044?3:n<16843260?4:5;}
    void mb(uint32_t n) {
        const unsigned count=mbBytes(n)-1;
        if(!count) {put(n,8);return;}
        constexpr uint32_t bases[]{0,252,508,66044,16843260};
        put(256-count,8);n-=bases[count];
        for(int i=int(count)-1;i>=0;--i)put(n>>(8*i),8);
    }
};
inline std::vector<uint8_t> fixture(int kind) {
    const int mode=std::min(kind,3),count=mode==0?2:mode==1?4:mode==2?16:0,bits=mode==0?1:mode==1?2:4;
    size_t cell_bits=8+(mode==2?12:0)+count*8;
    if(count)cell_bits+=size_t(Side)*Side*bits;
    else for(int y=0;y<Side;++y)for(int x=0;x<Side;++x)cell_bits+=Writer::mbBytes(pixel(kind,x,y))*8;
    const uint32_t payload_bytes=4+(cell_bits+7)/8; // dimensions, exponent and BlockHeader
    std::vector<uint8_t> out;out.reserve(payload_bytes+9);Writer writer{out};
    for(auto c:{'G','I','0','1'})writer.put(uint32_t(c),8);
    writer.mb(payload_bytes);writer.mb(Side);writer.mb(Side);writer.put(7,8);writer.put(0,8);
    writer.put(0x40|(mode<<4)|(mode==3?0:mode==0?3:15),8);
    if(mode==2)writer.put(4095,12);
    for(int i=0;i<count;++i)writer.mb(i*11);
    for(int y=0;y<Side;++y)for(int x=0;x<Side;++x) {
        const auto value=pixel(kind,x,y);
        if(count)writer.put(value/11,bits);else writer.mb(value);
    }
    require(out.size()==4+Writer::mbBytes(payload_bytes)+payload_bytes,"raw fixture length");
    return out;
}
template<bool Before> struct Raster {
    static constexpr bool benchmark_raw_before=Before;
    int width,height;
    uint32_t* data;
    void set(int x,int y,int64_t value) {
        require(value>=0 && uint64_t(value)<=UINT32_MAX,"raw code out of range");
        auto& out=data[size_t(y)*width+x];
        if(value && !out)out=uint32_t(value);
    }
};
inline uint64_t hash(const uint32_t* pixels,size_t count) {
    uint64_t h=14695981039346656037ull;
    for(size_t i=0;i<count;++i) {h^=pixels[i];h*=1099511628211ull;}return h;
}
template<bool Before> uint64_t measure(const std::vector<uint8_t>& bytes,const galuchat::Rect& target,uint32_t* pixels) {
    Raster<Before> dest{target.width,target.height,pixels};
    galuchat::BufferReader source(bytes);
    const auto start=monotonicMicros();
    {galuchat::GaluchatImageDataChunk01Reader reader(source);reader.readRect(target.x,target.y,dest);}
    const auto elapsed=monotonicMicros()-start;
    std::atomic_signal_fence(std::memory_order_seq_cst);
    require(source.atEnd() && !source.bitOffset(),"raw stream end");return elapsed;
}
} // namespace raw_cell
template<class Print,class Yield> void runRawCellBenchmark(Print print,Yield yield) {
    using namespace raw_cell;
    constexpr unsigned rounds=6,repeats=8;
    std::unique_ptr<uint32_t[]> pixels(new uint32_t[size_t(Side)*Side]);
    print("[RAW-CELL-BEGIN] side=128 rounds=%u repeats=%u output_bytes=65536 compression=RAW destination=uint32-zero-transparent\n",rounds,repeats);
    for(int kind=0;kind<5;++kind) {
        const auto bytes=fixture(kind);
        for(unsigned profile=0;profile<4;++profile) {
            const galuchat::Rect rect=profile==0?galuchat::Rect{0,0,128,128}:profile==1?galuchat::Rect{43,37,64,64}:
                profile==2?galuchat::Rect{63,0,1,128}:galuchat::Rect{63,63,1,1};
            const size_t count=size_t(rect.width)*rect.height;
            for(int y=0;y<rect.height;++y)for(int x=0;x<rect.width;++x)pixels[size_t(y)*rect.width+x]=pixel(kind,rect.x+x,rect.y+y);
            const auto expected=hash(pixels.get(),count);
            for(unsigned round=0;round<rounds;++round)for(unsigned slot=0;slot<2;++slot) {
                const bool before=(slot+round)%2;
                galuchat::detail::raw_benchmark_before_calls=0;galuchat::detail::raw_benchmark_cells=0;galuchat::detail::raw_benchmark_pixels=0;galuchat::detail::raw_benchmark_target=0;
                uint64_t elapsed=0;
                for(unsigned i=0;i<repeats;++i) {
                    std::fill_n(pixels.get(),count,0);
                    elapsed+=before?measure<true>(bytes,rect,pixels.get()):measure<false>(bytes,rect,pixels.get());
                }
                require(hash(pixels.get(),count)==expected,"raw image differs from oracle");
                require(galuchat::detail::raw_benchmark_cells==repeats,"raw dispatch count");
                require(galuchat::detail::raw_benchmark_before_calls==(before?repeats:0),"raw old/new dispatch");
                print("[RAW-CELL-CASE] backend=%s kind=%d profile=%u round=%u repeats=%u total_us=%llu bytes=%u hash=%llu cells=%u\n",
                    before?"before":"rows",kind,profile,round+1,repeats,(unsigned long long)elapsed,unsigned(bytes.size()),
                    (unsigned long long)expected,galuchat::detail::raw_benchmark_cells);
                yield();
            }
        }
    }
    print("[RAW-CELL-DONE] cases=240 images_equal=1 streams_equal=1\n");
}
} // namespace geoputer::benchmark

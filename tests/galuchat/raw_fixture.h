#pragma once
#include "rle_fixture.h"
namespace raw_fixture {
using namespace rle_fixture;
inline int paletteSize(int mode) { return mode==0?2:mode==1?4:mode==2?16:0; }
inline int pixelIndex(int r,int x,int y,int seed,int count) { return (x*7+y*3+seed+r)%count; }
inline int64_t value(int slot,bool delta=false) {
    if(delta)return slot-8;
    const uint64_t edges[]{0,251,252,507,508,66043,66044,16843259,16843260,UINT32_MAX};
    return static_cast<int64_t>(edges[slot%10]);
}
inline Bits encodedRaw(int r,int mode,int seed=0,bool delta=false) {
    Bits out;const int count=paletteSize(mode),width=mode==0?1:mode==1?2:4;
    out.put(0x40|(mode<<4)|(mode==3?0:mode==0?3:15),8);
    if(mode==2)out.put(4095,12);
    for(int i=0;i<count;++i) {
        if(delta)out.put(uint32_t(i<8?0x80|(8-i):i-8),8);
        else out.bytes(mb(uint64_t(value(i))));
    }
    for(int y=0;y<r;++y)for(int x=0;x<r;++x) {
        const int slot=pixelIndex(r,x,y,seed,count?count:10);
        if(count)out.put(slot,width);else out.bytes(mb(uint64_t(value(slot))));
    }
    return out;
}
inline Bytes rawBlock(int r,int mode,int seed,int compression,bool delta=false) {
    auto out=block(compression,encodedRaw(r,mode,seed,delta).data);
    if(delta)out[0]|=0x80;
    return out;
}
} // namespace raw_fixture

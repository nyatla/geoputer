#pragma once
// Measurement prototype: full-resolution edge detection folded into a small image.
#include "map/BoundaryExtractor.h"
#include "Config.h"
#include <galuchat/wgsmap3_reader.hpp>
#include "task/storage/galuchat/ReadTiming.h"
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
namespace geoputer::benchmark {
struct ScalePlan {
    map::ViewportPlan base;
    map::GridRect source;
    unsigned scale=1, high_width=0, high_height=0;
    int high_center_x=0, high_center_y=0;
    int sampleY(unsigned y) const {return base.center_y+int(std::lround((high_center_y-int(y))*base.grid_rows_per_pixel));}
};
inline ScalePlan scalePlan(double latitude,double longitude,int ux,int uy,unsigned scale) {
    if(scale!=1 && scale!=2 && scale!=4)throw std::runtime_error("unsupported test scale");
    ScalePlan p;p.scale=scale;
    if(map::planViewport(latitude,longitude,ux,uy,SIZE_MAX,p.base)!=map::Error::None)throw std::runtime_error("invalid test viewport");
    p.high_width=p.base.width*scale;p.high_height=p.base.height*scale;
    p.high_center_x=(p.base.width/2)*scale+scale/2;p.high_center_y=(p.base.height/2)*scale+scale/2;
    const double north=p.base.center_y+std::round(p.high_center_y*p.base.grid_rows_per_pixel);
    const double south=p.base.center_y+std::round((p.high_center_y-int(p.high_height)+1)*p.base.grid_rows_per_pixel);
    const int64_t west=int64_t(p.base.center_x)-p.high_center_x;
    if(north>INT32_MAX || south<INT32_MIN || north-south+1>INT32_MAX || west<INT32_MIN || west+p.high_width>INT32_MAX)
        throw std::runtime_error("test viewport overflow");
    p.source={int(west),int(south),int(p.high_width),int(north-south+1)};return p;
}
inline void foldBand(const ScalePlan& p,const map::RasterView& source,int core_y,unsigned rows,uint32_t current,map::BoundaryImage& out) {
    for(unsigned y=0;y<p.high_height;++y) {
        const int ay=p.sampleY(y);if(ay<core_y || int64_t(ay)>=int64_t(core_y)+rows)continue;
        const int cy=p.sampleY(std::min(y+1,p.high_height-1));
        if(cy<source.bounds.y || ay>=int64_t(source.bounds.y)+source.bounds.height)throw std::runtime_error("missing strip halo");
        for(unsigned x=0;x<p.high_width;++x) {
            const int ax=p.source.x+int(x);const auto a=source.get(ax,ay);
            const auto b=x+1<p.high_width?source.get(ax+1,ay):a;
            const auto c=y+1<p.high_height?source.get(ax,cy):a;
            if(a==b && a==c)continue;
            const uint8_t v=current && (a==current || b==current || c==current)?2:1;
            const auto dx=x/p.scale,dy=y/p.scale;
            if(v>out.get(dx,dy))out.set(dx,dy,v);
        }
    }
}
struct StripStats {size_t work_bytes=0;unsigned calls=0;uint64_t source_pixels=0,read_us=0,extract_us=0,yield_us=0;};
template<class Read,class Step> StripStats renderStrips(const ScalePlan& p,uint32_t current,unsigned band_rows,
                                                       map::BoundaryImage& out,Read read,Step step) {
    if(!band_rows)throw std::runtime_error("zero strip height");
    const unsigned halo=unsigned(std::ceil(p.base.grid_rows_per_pixel));
    const unsigned max_rows=std::min<unsigned>(p.source.height,band_rows+halo);
    if(uint64_t(p.source.width)*max_rows*sizeof(uint32_t)>config::MapViewCodeMaxBytes)throw std::runtime_error("strip exceeds work budget");
    StripStats stats;stats.work_bytes=size_t(p.source.width)*max_rows*sizeof(uint32_t);
    std::unique_ptr<uint32_t[]> pixels(new uint32_t[size_t(p.source.width)*max_rows]);
    if(!out.resize(p.base.width,p.base.height))throw std::bad_alloc();
    unsigned held_rows=0;
    for(unsigned offset=0;offset<unsigned(p.source.height);) {
        const unsigned count=std::min<unsigned>(band_rows,p.source.height-offset),carry=std::min(halo,offset);
        if(carry)std::memmove(pixels.get(),pixels.get()+size_t(held_rows-carry)*p.source.width,size_t(carry)*p.source.width*sizeof(uint32_t));
        const galuchat::Rect request{p.source.x,p.source.y+int(offset),p.source.width,int(count)};
        auto started=monotonicMicros();read(request,pixels.get()+size_t(carry)*p.source.width);stats.read_us+=monotonicMicros()-started;
        ++stats.calls;stats.source_pixels+=uint64_t(p.source.width)*count;
        const map::RasterView view{pixels.get(),{request.x,request.y-int(carry),request.width,int(carry+count)},size_t(carry+count)*p.source.width};
        started=monotonicMicros();foldBand(p,view,request.y,count,current,out);stats.extract_us+=monotonicMicros()-started;
        held_rows=carry+count;offset+=count;
        started=monotonicMicros();step();stats.yield_us+=monotonicMicros()-started;
    }
    return stats;
}
inline uint64_t boundaryHash(const map::BoundaryImage& image) {
    uint64_t h=14695981039346656037ull;for(size_t i=0;i<image.bytes();++i){h^=image.data()[i];h*=1099511628211ull;}return h;
}
} // namespace geoputer::benchmark

#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <type_traits>
#include <galuchat/wgsmap3_reader.hpp>

namespace {
using Decoder=galuchat::GaluchatImageDataChunk01Reader;
bool track=false;
size_t allocations=0;
void check(bool value,const char* why) {if(!value)throw std::runtime_error(why);}
struct Raster {
    int width=2,height=2;
    std::array<int64_t,4> pixels{};
    void set(int x,int y,int64_t value){pixels[size_t(y)*2+x]=value;}
};
constexpr uint8_t bytes[]{'G','I','0','1',9,2,2,1,0,0x70,11,12,13,14};
void decode(Decoder& reader,Raster& raster) {
    reader.readRect(0,0,raster);
    check(raster.pixels==std::array<int64_t,4>{11,12,13,14},"inline Reader changed pixels");
}
}
void* operator new(size_t bytes) {
    if(track)++allocations;
    if(auto* result=std::malloc(bytes?bytes:1))return result;
    throw std::bad_alloc();
}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
int main() {
    try {
        static_assert(!std::is_copy_constructible<Decoder>::value,"preserve noncopyable API");
        static_assert(!std::is_copy_assignable<Decoder>::value,"preserve noncopyable API");
        static_assert(std::is_nothrow_move_constructible<Decoder>::value,"preserve noexcept move");
        static_assert(std::is_nothrow_move_assignable<Decoder>::value,"preserve noexcept move assignment");
        allocations=0;track=true;
        {
            galuchat::BufferReader source(bytes,sizeof(bytes));Decoder reader(source);Raster raster;
            decode(reader,raster);check(source.atEnd(),"normal read changed end position");
        }
        {
            galuchat::BufferReader source(bytes,sizeof(bytes));
            auto reader=[&] {Decoder local(source);return Decoder(std::move(local));}();
            Raster raster;decode(reader,raster);check(source.atEnd(),"move constructor lost source");
        }
        {
            galuchat::BufferReader source(bytes,sizeof(bytes)),old_source(bytes,sizeof(bytes));
            Decoder reader(old_source);
            {Decoder local(source);reader=std::move(local);}
            Raster raster;decode(reader,raster);
            check(source.atEnd() && old_source.pos()==8,"move assignment failed to rebind source");
        }
        {
            galuchat::BufferReader source(bytes+8,sizeof(bytes)-8);Raster raster;
            Decoder::readRectFromBlockRange(source,{2,2,2,0,1,sizeof(bytes)-8},0,0,raster);
            check(source.atEnd() && raster.pixels==std::array<int64_t,4>{11,12,13,14},"range read changed");
        }
        track=false;
        check(!allocations,"chunk/range decoding still allocates payload Reader");
        std::cout<<"PASS: chunk/range decoding allocations=0, move construction/assignment and source lifetime preserved\n";
    }catch(const std::exception& error){track=false;std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}

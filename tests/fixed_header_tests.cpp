#include "task/storage/galuchat/FixedHeader.h"
#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>
#include <stdexcept>
namespace {bool tracking=false;unsigned allocations=0;void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
void* operator new(size_t n){if(tracking)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){
    for(const auto expected:{std::string_view("WGSMapSet/3"),std::string_view("GisWordBook/0"),std::string_view(""),std::string_view("0123456789abcdef")})
    for(unsigned mode=0;mode<4;++mode) {
        std::array<uint8_t,18> bytes{};std::memcpy(bytes.data(),expected.data(),expected.size());
        if(mode==1)bytes[0]='X';
        if(mode==2 && expected.size()+1<16)bytes[expected.size()+1]='X'; // Bytes after the first NUL are ignored.
        if(mode==3)std::fill_n(bytes.begin(),16,'A');
        bytes[16]=0xa5;
        galuchat::BufferReader reference(bytes.data(),bytes.size()),actual(bytes.data(),bytes.size());
        const bool want=galuchat::detail::readBStrAscii(reference,16)==expected;
        allocations=0;tracking=true;const bool got=geoputer::matchesFixedAscii16(actual,expected);tracking=false;
        check(got==want && actual.pos()==16 && !allocations,"padded comparison must preserve results/position without allocation");
        check(actual.readByte()==0xa5,"comparison read past the complete field");
    }
    for(unsigned length=0;length<16;++length) {
        std::array<uint8_t,16> bytes{};galuchat::BufferReader a(bytes.data(),length),b(bytes.data(),length);
        bool old_failed=false,new_failed=false;
        try{galuchat::detail::readBStrAscii(a,16);}catch(const std::exception&){old_failed=true;}
        try{geoputer::matchesFixedAscii16(b,"WGSMapSet/3");}catch(const std::exception&){new_failed=true;}
        check(old_failed && new_failed && a.pos()==b.pos(),"short fields must still fail at the same byte");
    }
    std::puts("PASS: fixed 16-byte headers preserve first-NUL/padding/EOF semantics with zero C++ allocations");
}

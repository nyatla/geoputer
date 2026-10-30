#pragma once
#include "WifiTypes.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace geoputer::wifi {
// Small retained bit matrix, not another display buffer. Encoder temporaries
// have bounded versions (5 for network / 2 for URL), with no heap fallback.
class QrCode {
public:
    static constexpr unsigned Area=100,QuietModules=4;
    bool update(const WifiView&,WifiPage);
    unsigned size()const{return size_;}
    unsigned cell()const{return size_?Area/(size_+QuietModules*2):0;}
    unsigned offset()const{return (Area-size_*cell())/2;}
    bool dark(unsigned x,unsigned y)const {
        if(x>=size_ || y>=size_)return false;
        const auto bit=y*size_+x;return modules_[bit/8] & (1u<<(7-(bit&7)));
    }
    const char* payload()const{return payload_;}
    bool valid()const{return size_!=0;}
private:
    std::array<uint8_t,(37*37+7)/8> modules_{};
    char payload_[96]{};
    uint8_t size_=0,version_=0;
};
bool connectionPayload(const WifiView&,char* out,size_t capacity);
}

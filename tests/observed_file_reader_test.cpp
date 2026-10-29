#include "application/task/storage/galuchat/ObservedFileReader.h"
#include <array>
#include <iostream>
#include <vector>
#include <unistd.h>
using namespace geoputer;
namespace {
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
template<class F>void fails(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"expected failure");}
struct Fixture {
    std::string native,path;std::vector<uint8_t> bytes;
    Fixture(size_t n):bytes(n){char p[]="/tmp/rgp-reader-XXXXXX";int fd=mkstemp(p);check(fd>=0,"tempfile");native=p;
        for(size_t i=0;i<n;++i)bytes[i]=uint8_t(i*131+(i>>8)*17);
        auto* f=fdopen(fd,"wb");check(f && fwrite(bytes.data(),1,n,f)==n,"write");fclose(f);
#ifdef GEOPUTER_TEST_FATFS
        path="/sd"+native;
#else
        path=native;
#endif
    }
    ~Fixture(){std::remove(native.c_str());}
};
struct Observer:StorageObserver {int depth=0;unsigned begins=0,ends=0;void sdBegin()override{++depth;++begins;}void sdEnd()override{--depth;++ends;}void cacheState(RgCacheState,bool=false)override{} };
}
int main(){try{
    Fixture f(32768);std::array<uint8_t,4096+32> guarded;guarded.fill(0xa5);
    BorrowedReadBuffer work(guarded.data()+16,4096);ReadFileSlot slot;Observer observer;FileIoTiming io;
    FileReadSource source(f.path,&observer,&io,{4096,4096},0,&slot);
    for(size_t skip:{size_t(0),size_t(100),size_t(4095),size_t(4096),size_t(8191),size_t(8192),size_t(8193)}) {
        source.withRange(37,20000,[&](galuchat::ByteReader& r){
            check(work.inUse() && slot.inUse(),"resources not leased");
            check(r.readByte()==f.bytes[37],"first byte");io={};r.skipInByte(skip);
            check(r.pos()==skip+1 && r.readByte()==f.bytes[38+skip],"skip byte/position");
            if(config::DetailedDiagnostics){const auto gap=skip>4095?skip-4095:0;check(io.seeks==(gap>4096?1u:0u),"short/long skip policy");}
        },&work);
        check(!work.inUse() && !slot.inUse(),"loan escaped read");
    }
    source.withRange(73,19,[&](galuchat::ByteReader& r){for(unsigned i=0;i<19;++i)check(r.readByte()==f.bytes[73+i],"range byte");check(r.atEnd(),"range EOF");fails([&]{r.readByte();});fails([&]{r.skipInByte(1);});},&work);
    source.withRange(0,20,[&](galuchat::ByteReader& r){const auto first=r.readBitsAsInt32(3);check(first==unsigned(f.bytes[0]>>5),"first bits");r.skipBits(10);check(r.readBitsAsInt32(3)==unsigned(f.bytes[1]&7),"bit skip");r.skipInByte(1);check(r.readByte()==f.bytes[3],"byte alignment");},&work);
    bool empty=false;source.withRange(f.bytes.size(),0,[&](galuchat::ByteReader& r){empty=r.atEnd();},&work);check(empty,"empty range");
    fails([&]{source.withRange(32768,1,[](auto&){},&work);});fails([&]{source.withRange(0,SIZE_MAX,[](auto&){},&work);});
    fails([&]{source.withRange(0,1,[](auto&){},nullptr);});
    FileReadSource missing_slot(f.path,nullptr);fails([&]{missing_slot.withRange(0,1,[](auto&){},&work);});
    FileReadSource too_big(f.path,nullptr,nullptr,{8192,0},0,&slot);fails([&]{too_big.withRange(0,20000,[](auto&){},&work);});
    source.withRange(0,10,[&](auto&){fails([&]{source.withRange(0,1,[](auto&){},&work);});std::array<uint8_t,16> b;BorrowedReadBuffer another(b.data(),b.size());fails([&]{source.withRange(0,1,[](auto&){},&another);});check(!another.inUse(),"failed FIL acquisition retained buffer");},&work);
    fails([&]{source.withRange(0,10,[](auto&){throw std::runtime_error("callback");},&work);});
#ifdef GEOPUTER_TEST_FATFS
    for(bool* flag:{&fatfs_test::fail_open,&fatfs_test::fail_seek,&fatfs_test::fail_read}){
        *flag=true;fails([&]{source.withRange(0,20,[](auto& r){r.readByte();},&work);});*flag=false;
        check(!work.inUse()&&!slot.inUse()&&!fatfs_test::live,"IO failure retained resources");
        source.withRange(0,1,[&](auto& r){check(r.readByte()==f.bytes[0],"recovery");},&work);
    }
    fatfs_test::fail_stat=true;fails([&]{FileReadSource bad(f.path,nullptr);});fatfs_test::fail_stat=false;
#endif
    for(unsigned i=0;i<16;++i)check(guarded[i]==0xa5&&guarded[4112+i]==0xa5,"buffer overrun");
    check(!work.inUse()&&!slot.inUse()&&observer.depth==0&&observer.begins==observer.ends,"final resource balance");
    if(!config::DetailedDiagnostics)check(!io.opens&&!io.reads&&!io.seeks&&!io.bytes,"diagnostics disabled");
    std::cout<<"PASS: explicit Reader buffer/FIL, short/long and bit skips, range/EOF, null/oversize/overlap rejection, exceptions and I/O recovery\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

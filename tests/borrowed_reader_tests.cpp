#include "application/task/storage/galuchat/ObservedFileReader.h"
#ifndef GEOPUTER_TEST_FATFS
#include "application/task/storage/StorageWorkArea.h"
#endif
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <unistd.h>
using namespace geoputer;
namespace {
bool tracking=false;size_t allocs=0,bytes=0,reader_allocs=0,fil_allocs=0;
#ifdef GEOPUTER_TEST_FATFS
bool order_tracking=false;void* order_buffer=nullptr;void* order_file=nullptr;
unsigned release_count=0,buffer_release=0,file_release=0;
#endif

void check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
template<class F>void rejected(F call) {bool failed=false;try{call();}catch(const std::exception&){failed=true;}check(failed,"expected rejection");}
struct Fixture {
    std::string path;
    std::vector<uint8_t> data;
    Fixture():data(131072) {
        for(size_t i=0;i<data.size();++i)data[i]=uint8_t(i*131+(i>>8)*17);
        char name[]="/tmp/rgp-borrowed-XXXXXX";const int fd=mkstemp(name);check(fd>=0,"mkstemp");path=name;
        auto* f=fdopen(fd,"wb");check(f,"fdopen");check(std::fwrite(data.data(),1,data.size(),f)==data.size(),"write");std::fclose(f);
    }
    ~Fixture(){std::remove(path.c_str());}
    std::string readPath()const {
#ifdef GEOPUTER_TEST_FATFS
        return "/sd"+path;
#else
        return path;
#endif
    }
};
struct Counts {size_t allocs,bytes;};
Counts creation(ObservedFileReaderFactory& factory) {
    allocs=bytes=0;tracking=true;
    {auto reader=factory.createRange(0,65536);check(reader->readByte()==0,"creation data");}
    tracking=false;return {allocs,bytes};
}
void equalIo(const FileIoTiming& a,const FileIoTiming& b) {
    check(a.bytes==b.bytes && a.reads==b.reads && a.seeks==b.seeks && a.opens==b.opens && a.closes==b.closes,"borrow changed I/O operations");
}
}
void* operator new(size_t n){
    if(tracking){++allocs;bytes+=n;reader_allocs+=n==sizeof(ObservedFileReader);
#ifdef GEOPUTER_TEST_FATFS
        fil_allocs+=n==sizeof(FIL);
#endif
    }
    if(auto*p=std::malloc(n?n:1)) {
#ifdef GEOPUTER_TEST_FATFS
        if(order_tracking) {if(n==4096)order_buffer=p;else if(n==sizeof(FIL))order_file=p;}
#endif
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{
#ifdef GEOPUTER_TEST_FATFS
    if(order_tracking && p) {if(p==order_buffer)buffer_release=++release_count;else if(p==order_file)file_release=++release_count;}
#endif
    std::free(p);
}
void operator delete[](void*p)noexcept{::operator delete(p);}
void operator delete(void*p,size_t)noexcept{::operator delete(p);}void operator delete[](void*p,size_t)noexcept{::operator delete(p);}
int main(){try {
    Fixture file;
    // Exact-fit conversion, all decimal drive widths, UTF-8 bytes and bounds.
    const std::string conversion_path="/sd/dataset/01/行政区域.glc";
    std::array<char,514> converted{};
    for(unsigned drive:{0u,9u,10u,99u,100u,255u}) {
        const auto expected=fatFsPath(static_cast<uint8_t>(drive),conversion_path);
        converted.fill('!');allocs=bytes=0;tracking=true;
        const auto* path=fatFsPath(static_cast<uint8_t>(drive),conversion_path,{converted.data()+1,expected.size()+1});
        tracking=false;
        check(!allocs && !std::strcmp(path,expected.c_str()),"borrowed path conversion allocated or changed bytes");
        check(converted.front()=='!' && converted[expected.size()+2]=='!',"path conversion overran capacity");
        rejected([&]{fatFsPath(static_cast<uint8_t>(drive),conversion_path,{converted.data()+1,expected.size()});});
    }
    rejected([&]{fatFsPath(0,conversion_path,{nullptr,512});});
    rejected([&]{fatFsPath(0,"/outside/data.glc",{converted.data(),converted.size()});});
    std::array<uint8_t,6060+32> guarded;guarded.fill(0xa5);
    BorrowedReadBuffer pool(guarded.data()+16,6060);
    FileIoTiming owned_io{},borrowed_io{};
    ObservedFileReaderFactory owned(file.readPath(),nullptr,&owned_io,{4096,4096});
    ObservedFileReaderFactory borrowed(file.readPath(),nullptr,&borrowed_io,{4096,4096},0,&pool);
    {auto a=owned.create();auto b=borrowed.create();a->readByte();b->readByte();}
    const auto before=creation(owned),after=creation(borrowed);
    check(before.allocs==after.allocs+1 && before.bytes==after.bytes+4096,"expected exactly one 4096-byte allocation removed");
    std::cout<<"allocation calls="<<before.allocs<<"->"<<after.allocs<<" bytes="<<before.bytes<<"->"<<after.bytes<<'\n';
    owned_io=borrowed_io={};
    {
        auto a=owned.createRange(37,90000),b=borrowed.createRange(37,90000);
        check(pool.inUse(),"borrow not retained");
        rejected([&]{borrowed.createRange(0,1024);});
        uint32_t rng=0x12345678;
        galuchat::BufferReader golden(file.data.data()+37,90000);
        for(unsigned step=0;step<2000;++step) {
            rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
            const size_t available=golden.remaining()*8+golden.bitOffset();if(!available)break;
            switch(rng%4) {
                case 0:{const int n=int(std::min<size_t>(1+(rng>>4)%31,available));const auto want=golden.readBitsAsInt32(n);check(a->readBitsAsInt32(n)==want && b->readBitsAsInt32(n)==want,"bits");break;}
                case 1:{const auto n=std::min<size_t>((rng>>4)%8193,available/8);a->skipInByte(n);b->skipInByte(n);golden.skipInByte(n);break;}
                case 2:{const auto n=std::min<size_t>((rng>>4)%131073,available);a->skipBits(n);b->skipBits(n);golden.skipBits(n);break;}
                default:a->skipToByte();b->skipToByte();golden.skipToByte();
            }
            check(a->pos()==b->pos() && b->pos()==golden.pos() && a->bitOffset()==b->bitOffset() && a->atEnd()==b->atEnd(),"reader state");
        }
    }
    check(!pool.inUse(),"destructor did not release");equalIo(owned_io,borrowed_io);
    // Two factories may lend the same bytes only after the previous Reader
    // dies: index -> GLC -> index, with explicit cross-factory overlap checks.
    ObservedFileReaderFactory glc(file.readPath(),nullptr,nullptr,{config::MapRectReadBufferBytes,4096},0,&pool);
    {auto index=borrowed.createRange(0,8192);check(index->readByte()==0,"index contents");
     rejected([&]{glc.createRange(0,8192);});}
    {auto map=glc.createRange(73,9000);check(map->readByte()==file.data[73],"GLC shared contents");
     rejected([&]{borrowed.createRange(0,8192);});}
    {auto index=borrowed.createRange(1,8192);check(index->readByte()==file.data[1],"index reuse after GLC");}
    check(!pool.inUse(),"cross-factory sharing leaked loan");
    for(size_t n:{1u,123u,4095u,4096u,4097u,16384u,65536u}) {
        auto reader=borrowed.createRange(73,n);for(size_t i=0;i<n;++i)check(reader->readByte()==file.data[73+i],"range contents");
        rejected([&]{reader->readByte();});check(reader->atEnd(),"EOF changed position");
    }
    {
        auto empty=borrowed.createRange(file.data.size(),0);check(empty->atEnd() && !pool.inUse(),"empty range leased scratch");
        auto reader=borrowed.createRange(0,128);check(pool.inUse(),"nonempty lease");
        rejected([&]{borrowed.createRange(0,file.data.size()+1);});check(pool.inUse(),"bad range released another Reader's lease");
    }
    check(!pool.inUse(),"range cleanup");
    ObservedFileReaderFactory too_big(file.readPath(),nullptr,nullptr,{16384,4096},0,&pool);
    rejected([&]{too_big.create();});check(!pool.inUse(),"undersized storage acquired");
    {auto small=too_big.createRange(0,32);check(small->readByte()==file.data[0],"actual range not capped");}
    {auto map=std::make_unique<ObservedFileReader>(file.readPath(),file.data.size(),0,nullptr,&owned_io,FileReadOptions{16384,4096});
     auto index=borrowed.create();owned_io=borrowed_io={};map->readByte();index->readByte();
     if(config::DetailedDiagnostics)check(owned_io.bytes==16384 && borrowed_io.bytes==4096,"map 16KiB / index 4KiB must remain distinct");}
    for(size_t i=0;i<16;++i)check(guarded[i]==0xa5 && guarded[6060+16+i]==0xa5,"borrow overran sentinels");
    for(size_t i=4096+16;i<6060+16;++i)check(guarded[i]==0xa5,"read used more than configured 4KiB");
    rejected([&]{ObservedFileReader bad(file.readPath()+".missing",file.data.size(),0,nullptr,nullptr,{},0,&pool);});
    check(!pool.inUse(),"open failure leaked lease");
#ifdef GEOPUTER_TEST_FATFS
    fatfs_test::fail_open=true;order_tracking=true;
    rejected([&]{auto reader=owned.create();});
    order_tracking=false;fatfs_test::fail_open=false;
    check(order_buffer && order_file && file_release && buffer_release>file_release && !fatfs_test::live,
          "constructor failure must free FIL before its older owned read buffer");
    for(bool* flag:{&fatfs_test::fail_open,&fatfs_test::fail_seek,&fatfs_test::fail_read}) {
        *flag=true;
        rejected([&]{auto reader=borrowed.create();reader->readByte();});
        *flag=false;check(!pool.inUse() && !fatfs_test::live,"I/O exception leaked lease/file");
    }
#endif
    ReadFileSlot slot;
    ObservedFileReaderFactory scoped(file.readPath(),nullptr,nullptr,{4096,4096},0,&pool,&slot);
    std::array<uint8_t,4096> second_bytes{};BorrowedReadBuffer second_pool(second_bytes.data(),second_bytes.size());
    ObservedFileReaderFactory second(file.readPath(),nullptr,nullptr,{4096,4096},0,&second_pool,&slot);
    for(unsigned attempt=0;attempt<3;++attempt) {
        reader_allocs=fil_allocs=0;tracking=true;
        scoped.withRange(73,9000,[&](galuchat::ByteReader& reader){
            check(pool.inUse() && slot.inUse(),"scoped Reader did not borrow both resources");
            check(reader.readByte()==file.data[73],"scoped Reader bytes");
            reader.skipInByte(8191);check(reader.readByte()==file.data[8265],"scoped Reader seek/skip");
        });
        tracking=false;
        check(!reader_allocs && !fil_allocs,"stack Reader or borrowed FIL was allocated on heap");
        check(!pool.inUse() && !slot.inUse(),"scoped Reader retained a resource");
    }
    scoped.withRange(0,32,[&](galuchat::ByteReader&){
        rejected([&]{second.withRange(0,32,[](galuchat::ByteReader&){});});
        check(pool.inUse() && slot.inUse() && !second_pool.inUse(),"double FIL loan damaged owner");
    });
    scoped.withRange(0,0,[&](galuchat::ByteReader& reader){check(reader.atEnd() && !pool.inUse() && !slot.inUse(),"empty range acquired resources");});
    rejected([&]{scoped.withRange(0,32,[&](galuchat::ByteReader& reader){reader.readByte();throw std::runtime_error("decoder failure");});});
    check(!pool.inUse() && !slot.inUse(),"consumer exception leaked resources");
#ifdef GEOPUTER_TEST_FATFS
    for(bool* flag:{&fatfs_test::fail_open,&fatfs_test::fail_seek,&fatfs_test::fail_read}) {
        *flag=true;rejected([&]{scoped.withRange(0,32,[](galuchat::ByteReader& reader){reader.readByte();});});
        *flag=false;check(!pool.inUse() && !slot.inUse() && !fatfs_test::live,"scoped I/O error leaked FIL or buffer");
    }
#endif
#ifdef GEOPUTER_TEST_FATFS
    {
        std::array<uint8_t,6060+32> image;image.fill(0xa5);
        BorrowedReadBuffer read_buffer(image.data()+16,4096);ReadFileSlot fixed_file;
        const std::string selected_path=file.readPath();
        NativePathBuffer native{reinterpret_cast<char*>(image.data()+16+4096),config::MapNativePathBytes};
        fatfs_test::last_open.reserve(512);fatfs_test::last_stat.reserve(512);
        allocs=bytes=0;tracking=true;
        {
            ObservedFileReaderFactory index(std::cref(selected_path),nullptr,nullptr,{4096,4096},0,&read_buffer,&fixed_file,native);
            ObservedFileReaderFactory map(std::cref(selected_path),nullptr,nullptr,{4096,4096},0,&read_buffer,&fixed_file,native);
            for(auto* factory:{&index,&map})factory->withRange(73,9000,[&](galuchat::ByteReader& reader){
                check(reader.readByte()==file.data[73],"borrowed-path read changed data");
                // FatFs only needs the path during open, never during later reads.
                std::memset(native.data,'?',native.capacity);
                reader.skipInByte(8191);check(reader.readByte()==file.data[8265],"native scratch corrupted Reader bytes");
            });
        }
        tracking=false;
        check(!allocs && !bytes,"scoped map/index path, Reader or FIL allocated");
        check(!read_buffer.inUse() && !fixed_file.inUse() && !fatfs_test::live,"native path retained a lease");
        for(size_t i=0;i<16;++i)check(image[i]==0xa5 && image[16+6060+i]==0xa5,"native path crossed UI loan bounds");
        for(size_t i=16+4096+config::MapNativePathBytes;i<16+6060;++i)check(image[i]==0xa5,"native path used unrelated tail bytes");
        NativePathBuffer short_path{native.data,2};
        rejected([&]{ObservedFileReader reader(selected_path,file.data.size(),0,nullptr,nullptr,{4096,0},0,&read_buffer,&fixed_file,short_path);});
        check(!read_buffer.inUse() && !fixed_file.inUse() && !fatfs_test::live,"short native path leaked FIL or buffer");
        for(bool* flag:{&fatfs_test::fail_stat,&fatfs_test::fail_open,&fatfs_test::fail_seek,&fatfs_test::fail_read}) {
            *flag=true;
            rejected([&]{ObservedFileReaderFactory factory(std::cref(selected_path),nullptr,nullptr,{4096,0},0,&read_buffer,&fixed_file,native);
                factory.withRange(0,32,[](galuchat::ByteReader& reader){reader.readByte();});});
            *flag=false;
            check(!read_buffer.inUse() && !fixed_file.inUse() && !fatfs_test::live,"native path I/O failure leaked resources");
        }
        std::cout<<"PASS: borrowed path + index/GLC stack Reader + fixed FIL C++ allocations=0, bounds and errors\n";
    }
#endif
    std::cout<<"PASS: stack Reader/FIL allocations=0, single FIL lease, callback/I/O failure cleanup\n";
    for(unsigned i=0;i<100;++i){auto reader=borrowed.createRange(i,100);check(reader->readByte()==file.data[i],"reuse after errors");}
#ifndef GEOPUTER_TEST_FATFS
    StorageWorkArea work;auto* job=work.forMap();map::CodeImageBuffer session_pixels{};map::MapSession session(session_pixels);map::Request req{};req.token=1;req.screen_generation=1;req.dataset_generation=1;
    map::FixedBoundaryImage back;req.buffer={back.storageData(),back.Capacity,1};
    check(job && !job->readBuffer() && job->start(req,session),"job start");
    auto* memory=job->readBuffer();check(memory,"read phase provides scratch");
    check(job->pathBuffer().data==reinterpret_cast<char*>(req.buffer.data+4096)
          && job->pathBuffer().capacity==config::MapNativePathBytes,"path scratch must use the UI loan tail");
    {
        ObservedFileReaderFactory factory(file.readPath(),nullptr,nullptr,{4096,0},0,memory);
        auto reader=factory.create();reader->readByte();
        check(!job->release(req) && !work.forLogs(),"active borrowed Reader allowed job destruction");
        job->loaded({});check(job->info().error==map::Error::InvalidInput,"render began before Reader destruction");
        check(reader->readByte()==file.data[1],"failed phase transition overwrote read buffer");
    }
    check(!memory->inUse() && job->release(req) && work.forLogs(),"closed Reader allows Union switch");
    job=work.forMap();++req.token;check(job && job->start(req,session),"restart map job for FIL-only guard");
    {
        ReadFile file_handle(0,job->readFile());check(file_handle.openRead(file.readPath()),"open job FIL");
        check(!job->readBuffer()->inUse() && !job->release(req) && !work.forLogs(),"active FIL allowed Union replacement");
    }
    check(!job->readFile()->inUse() && job->release(req) && work.forLogs(),"closed FIL allows Union replacement");


#endif
    std::cout<<"PASS: borrowed lifetime, no free/overrun, double-borrow rejection, ranges, mixed bits/skips, matching I/O, errors, 16KiB map isolation and job switching\n";
}catch(const std::exception&e){tracking=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

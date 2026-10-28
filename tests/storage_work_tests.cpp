#include "task/storage/StorageWorkArea.h"
#include "task/storage/StorageScreenResources.h"
#include "task/storage/StorageReplies.h"
#include "map/MapFrameReceiver.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdexcept>
#include <filesystem>

using namespace geoputer;
namespace {
bool track=false,fail_close=false;
unsigned allocations=0;
void check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
}
void* operator new(size_t bytes) {
    if(track)++allocations;
    if(auto* p=std::malloc(bytes?bytes:1))return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}
extern "C" int __real_fclose(FILE*);
extern "C" int __wrap_fclose(FILE* file) {
    const auto result=__real_fclose(file);
    if(fail_close){fail_close=false;return EOF;}return result;
}
int main() {
    {
        map::CodeImageBuffer fixed{};StorageScreenResources resources(fixed);
        auto& live=resources.live_map;auto& history=resources.log_map;
        check(live.pixels==fixed.data() && history.pixels==fixed.data(),"one fixed map array with two logical session identities");
        live.plan.source={0,0,1,1};live.plan.width=live.plan.height=1;
        live.info.width=live.info.height=1;live.valid=true;fixed[0]=0x12345678;
        map::Request r{};r.token=21;r.screen_generation=1;r.dataset_generation=9;r.latitude=35;r.longitude=140;
        map::FixedBoundaryImage output;r.buffer={output.storageData(),output.Capacity,1};
        live.info.request=r;
        auto next=r;next.screen_generation=2;++next.token;
        auto* job=resources.work.forMap();check(job && job->start(next,live) && !job->reading(),"fresh UI generation reuses a matching retained raster without SD");
        while(!job->complete())job->step();
        check(job->info().request.screen_generation==2 && job->info().region==fixed[0] && job->release(next),"cached frame transfers under the new screen identity");
        allocations=0;track=true;
        for(unsigned i=0;i<100;++i) {resources.selectMap(map::SessionId::Log);history.valid=true;resources.selectMap(map::SessionId::Live);live.reserve(fixed.size());}
        track=false;
        check(!allocations && !history.valid && live.pixels==fixed.data() && fixed[0]==0x12345678,"switching owners invalidates metadata, not storage, with zero allocations");
        bool rejected=false;try {live.reserve(fixed.size()+1);}catch(const std::bad_alloc&){rejected=true;}
        check(rejected && live.pixels==fixed.data(),"oversized raster cannot allocate a fallback");
    }
    StorageWorkArea work;work.setDrive(0);
    check(work.activeLogs() && !work.activeMap(),"initial reader is live");
    LogReadRequest read{};read.kind=LogReadKind::List;read.token=22;
    check(work.logs().begin(read) && !work.forMap(),"unfinished reader excludes a map");
    work.logs().cancel();check(work.forMap(),"cancelled and closed reader permits map");
    map::CodeImageBuffer session_pixels{};map::MapSession session(session_pixels);session.plan.width=240;session.plan.height=101;
    session.plan.source={0,0,240,101};session.plan.grid_rows_per_pixel=1;
    session.plan.center_x=120;session.plan.center_y=50;
    session.reserve(240*101);session.valid=true;
    std::fill_n(session.pixels,240*101,0);
    map::Request request{};request.token=1;request.screen_generation=1;request.dataset_generation=1;
    map::FixedBoundaryImage output;request.buffer={output.storageData(),output.Capacity,1};
    session.info.request=request;session.info.width=240;session.info.height=101;
    request.reuse=true;
    auto* map=work.activeMap();
    check(map->start(request,session) && !work.forLogs(),"map generation excludes reader");
    map->fail(map::Error::ReadError);
    check(map->complete() && !work.forLogs(),"failed result remains retained until release");
    auto wrong=request;++wrong.token;
    check(!map->release(wrong) && !work.forLogs(),"stale release cannot invalidate active pixels");
    check(map->release(request) && work.forLogs(),"released job permits reader");

    // Simulate a still-open read handle. A close error must leave the same
    // alternative active even when host fclose has already consumed the handle.
    const auto path=std::filesystem::temp_directory_path()/"rgp-storage-work-close.dat";
    {auto* file=std::fopen(path.c_str(),"wb");check(file,"fixture open");std::fputs("x",file);std::fclose(file);}
    auto* logs=work.activeLogs();check(logs->file_.openRead(path.string()),"open retained test handle");
    fail_close=true;
    check(!work.forMap() && work.activeLogs()==logs && !fail_close,"close failure preserves active reader and refuses overwrite");
    check(work.forMap(),"retry after close recovery succeeds");std::filesystem::remove(path);

    // Long directories remain supported; repeated scans reuse one path allocation.
    const auto list_dir=std::filesystem::temp_directory_path()/"rgp-reusable-log-path"/std::string(180,'x');
    std::filesystem::create_directories(list_dir);
    for(unsigned i=0;i<32;++i) {
        char name[48];std::snprintf(name,sizeof(name),"20261026_0100%02u_0.gps.dat",i);
        auto* file=std::fopen((list_dir/name).c_str(),"wb");check(file,"list fixture");std::fclose(file);
    }
    {
        LogReader reader(list_dir.string());LogReadRequest list{};list.kind=LogReadKind::List;list.token=99;
        allocations=0;track=true;
        check(reader.begin(list),"first list accepted");while(!reader.done())reader.step();
        track=false;
        check(reader.result().error==LogReadError::None && reader.result().count==LogListRows && allocations==1,
              "first scan allocates one reusable path, regardless of directory entry count");
        const auto before=reader.result();const auto* path=reader.path_.data();
        allocations=0;track=true;
        check(reader.begin(list),"repeat list accepted");while(!reader.done())reader.step();
        track=false;
        check(!allocations && reader.path_.data()==path && !std::memcmp(before.data.list.names,reader.result().data.list.names,sizeof(before.data.list.names)),
              "repeated scan has identical sorted names and no C++ path allocation");
        list.kind=LogReadKind::Summary;std::strcpy(list.name,before.data.list.names[0]);
        allocations=0;track=true;
        check(reader.begin(list),"summary accepted");while(!reader.done())reader.step();
        track=false;
        check(!allocations && reader.result().error==LogReadError::Header,"summary reuses path and still rejects an empty file");
    }
    std::filesystem::remove_all(list_dir.parent_path());

    // Screen exit only releases a terminal, closed reader from that generation.
    {
        auto* logs=work.forLogs();check(logs,"reader workspace for scope test");
        logs->path_.assign(80,'x');
        LogReadRequest read{};read.kind=LogReadKind::List;read.screen_generation=11;
        check(logs->begin(read) && !work.releaseLogs(11) && !logs->path_.empty(),"active read excludes scope release");
        logs->cancel();check(work.releaseLogs(10) && !logs->path_.empty(),"old scope release preserves a newer reader");
        check(logs->file_.openRead(path.string())==false,"removed fixture is absent");
        {auto* f=std::fopen(path.c_str(),"wb");check(f,"release fixture");std::fclose(f);}
        check(logs->file_.openRead(path.string()),"open retained read handle");
        fail_close=true;
        check(!work.releaseLogs(11) && !logs->path_.empty(),"close failure preserves reader and path");
        check(work.releaseLogs(11) && logs->path_.empty() && logs->path_.capacity()==std::string{}.capacity(),"successful retry releases path capacity");
        std::filesystem::remove(path);check(work.forMap() && work.releaseLogs(11),"late reader release is harmless to map work");
    }
    // Writer close failures retain their retry identity; a completed retry frees it.
    {
        const auto writer_dir=std::filesystem::temp_directory_path()/"rgp-writer-close-path";
        std::filesystem::remove_all(writer_dir);std::filesystem::create_directory(writer_dir);
        LogWriter writer(writer_dir.string());LogCommand point{};point.session_id=777;point.sample_seq=1;point.fix={35.0,140.0,0,true};
        check(writer.open(777,UINT64_MAX)==LogError::None && writer.append(point)==LogError::None && writer.flush(1),"write close-retry fixture");
        const auto old_path=writer.path();check(!old_path.empty(),"writer has retry identity");
        check(writer.requestFlush(777,1,true)==LogError::None,"request writer close");fail_close=true;
        check(!writer.flush(2) && writer.path()==old_path && writer.snapshot().closing,"failed close retains path and closing state");
        check(writer.flush(3) && writer.path().empty() && writer.path().capacity()==std::string{}.capacity() && !writer.snapshot().closing,
              "completed close retry releases path capacity outside critical section");
        check(std::filesystem::exists(old_path) && writer.snapshot().name[0],"saved file and name remain after path release");
        std::filesystem::remove_all(writer_dir);
    }

    // A live index Reader prevents returning the buffer to UI or changing work.
    map::MapFrameReceiver receiver;
    const auto retained_plan=session.plan;session.valid=false;
    request.buffer={};request.reuse=false;++request.token;
    check(receiver.begin(request,request.buffer),"lend UI pool to index Reader");
    auto* reading_job=work.forMap();check(reading_job && reading_job->start(request,session),"start borrowed index job");
    {
        FileReadBuffer reader;reader.resize(4096,reading_job->readBuffer());
        receiver.clear();std::fill_n(reader.data(),reader.size(),0x5a);
        auto other=request;other.buffer={};++other.token;map::BufferLease denied{};
        check(!reading_job->release(request) && !work.forLogs() && !receiver.begin(other,denied),
              "reader borrowing prevents cancellation return and UI pool reuse after screen leave");
    }
    check(reading_job->release(request) && receiver.discard(request),"return loan only after Reader destruction");
    session.plan=retained_plan;session.info.request=request;session.info.width=240;session.info.height=101;
    session.info.meters_per_pixel=1;session.valid=true;request.reuse=true;
    // Each update reuses two fixed UI arrays, including workspace replacement.
    uint8_t* slots[2]{};
    allocations=0;track=true;
    for(unsigned i=0;i<100;++i) {
        request.buffer={};++request.token;
        check(receiver.begin(request,request.buffer),"lend fixed back image");
        if(i<2)slots[i]=request.buffer.data;
        else check(slots[i%2]==request.buffer.data,"UI image addresses must not change");
        auto* job=work.forMap();
        check(job && job->start(request,session),"start reused source in shared area");
        while(!job->complete())job->step();
        check(job->info().error==map::Error::None,"direct output generation succeeds");
        const auto done=job->info();
        check(job->release(request) && receiver.commit(done) && work.forLogs(),"return write ownership, publish fixed output, then reuse worker");
    }
    track=false;check(!allocations,"100 reader/map switches, UI loans and boundary generations allocate no heap");
    check(session.valid && session.pixels,"switching jobs never releases the screen-owned raster");
    std::puts("PASS: Storage shared job lifetime, close-failure retention, cancellation, stale request and 100 allocation-free switches");
}

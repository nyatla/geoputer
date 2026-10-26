#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <signal.h>
#include <stdexcept>
#include <sys/resource.h>
#include <thread>
#include <vector>
#include "task/storage/log/LogWriter.h"

using namespace geoputer;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct BlockIo : StorageObserver {
    std::mutex mutex;
    std::condition_variable ready;
    bool armed=false, entered=false, released=false;
    void arm() { std::lock_guard<std::mutex> guard(mutex); armed=true; entered=released=false; }
    void sdBegin() override {
        std::unique_lock<std::mutex> guard(mutex);
        if (!armed) return;
        entered=true; ready.notify_all();
        ready.wait(guard,[&]{ return released; });
        armed=false;
    }
    bool wait() {
        std::unique_lock<std::mutex> guard(mutex);
        return ready.wait_for(guard,std::chrono::seconds(2),[&]{ return entered; });
    }
    void release() { std::lock_guard<std::mutex> guard(mutex); released=true; ready.notify_all(); }
    void sdEnd() override {}
    void cacheState(RgCacheState, bool) override {}
};
LogCommand point(unsigned n) {
    LogCommand value{}; value.session_id=123; value.sample_seq=n;
    value.fix={35.0+n*0.00001,140.0,0,true};
    value.fix.fields=GpsTime; value.fix.utc_epoch_ms=1789309921000ull+n*1000ull;
    return value;
}
std::vector<uint8_t> read(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void concurrentAndRetry(const std::string& path) {
    BlockIo io;
    LogWriter writer(path,&io);
    check(writer.open(123,0)==LogError::None,"ring session opens");
    for(unsigned n=1;n<=40;++n) check(writer.append(point(n),n*10)==LogError::None,"initial points accepted");
    const auto old=writer.snapshot();
    bool saved=false;
    io.arm();
    std::thread consumer([&]{ saved=writer.flush(1000); });
    const bool entered=io.wait();
    bool appended=entered;
    if(entered) {
        for(unsigned n=41;n<=64;++n) appended &= writer.append(point(n),n*10)==LogError::None;
        appended &= writer.append(point(65),650)==LogError::BufferFull;
        const auto during=writer.snapshot();
        appended &= during.accepted==64 && during.saved==0 && during.buffered==64;
    }
    io.release(); consumer.join();
    check(entered && appended && saved,"App can append up to the 64-slot limit while Storage holds an I/O batch");
    const auto first=writer.snapshot();
    check(first.saved==40 && first.saved_seq==40 && first.buffered==24 && first.accepted==64 && first.buffer_since_ms==410,
          "commit releases only its pinned batch and keeps the oldest concurrent append time");
    auto merged=first;
    check(!applyLogSnapshot(merged,old) && merged.saved==40 && merged.accepted==64,
          "older Storage notification cannot undo App acceptance or persistence");
    check(read(writer.path()).size()==512+40*32,"first I/O does not accidentally include records appended during it");

    // Head=40, 50 records: this batch wraps into two spans (24 + 26 records).
    for(unsigned n=65;n<=90;++n) check(writer.append(point(n),n*10)==LogError::None,"ring tail wraps without copying");
    struct rlimit original{},limited{};
    check(getrlimit(RLIMIT_FSIZE,&original)==0,"read partial-write limit");
    limited=original; limited.rlim_cur=512+65*32;
    const auto previous=signal(SIGXFSZ,SIG_IGN);
    check(setrlimit(RLIMIT_FSIZE,&limited)==0,"force failure after the first wrapped span");
    io.arm();
    std::thread failing([&]{ saved=writer.flush(2000); });
    const bool retry_entered=io.wait();
    bool suffix=retry_entered;
    if(retry_entered) for(unsigned n=91;n<=100;++n) suffix &= writer.append(point(n),n*10)==LogError::None;
    io.release(); failing.join();
    const bool restored=setrlimit(RLIMIT_FSIZE,&original)==0;
    signal(SIGXFSZ,previous);
    check(restored && retry_entered && suffix && !saved,"wrapped write fails while concurrent records remain accepted");
    auto failed=writer.snapshot();
    check(failed.saved==40 && failed.buffered==60 && failed.accepted==100 && failed.error==LogError::Io,
          "partial-write failure retains both the pinned batch and its later suffix");
    for(unsigned n=101;n<=104;++n) check(writer.append(point(n),n*10)==LogError::None,"remaining free slots usable after failure");
    check(writer.snapshot().error==LogError::Io && writer.append(point(105))==LogError::BufferFull,
          "RAM acceptance does not erase the write error or overwrite retained data");
    check(writer.requestFlush(123,104,true)==LogError::None,"close fixes the boundary including the later suffix");
    check(writer.append(point(105))==LogError::Invalid,"close rejects further App additions");
    check(writer.flush(3000),"failed wrapped batch can retry at its original offset");
    const auto retry=writer.snapshot();
    check(retry.saved==90 && retry.saved_seq==90 && retry.buffered==14 && retry.closing &&
          retry.buffer_since_ms==910 && writer.needsFlush(),
          "retry keeps its original batch size and preserves the later close/flush request");
    check(writer.flush(4000),"remaining suffix persists and file closes");
    const auto complete=writer.snapshot();
    check(complete.saved==104 && complete.buffered==0 && !complete.open && !complete.closing && !writer.needsFlush(),
          "all accepted records finish exactly once");
    const auto file=read(path+"/"+writer.snapshot().name);
    check(file.size()==512+104*32 && validLogHeader(file.data()),"ring wrap and retry preserve the portable file format");
    for(unsigned n=1;n<=104;++n) {
        uint8_t expected[32];
        check(encodeLogRecord(point(n).fix,expected) && std::memcmp(file.data()+512+(n-1)*32,expected,32)==0,
              "every file record has the original order, contents and CRC after wrap/retry");
    }
    check(writer.open(124,0)==LogError::None,"next session starts after complete close");
    auto latest=complete;
    check(applyLogSnapshot(latest,writer.snapshot()) && latest.session_id==124 && latest.accepted==0,
          "log revision advances across sessions instead of resetting with counters");
}
void parallelStream(const std::string& path) {
    LogWriter writer(path);
    check(writer.open(123,0)==LogError::None,"parallel stream opens");
    constexpr unsigned total=2048;
    std::atomic<bool> done{false},failed{false};
    std::thread producer([&]{
        for(unsigned n=1;n<=total && !failed;++n) {
            LogError error;
            do {
                error=writer.append(point(n),n);
                if(error==LogError::BufferFull) std::this_thread::yield();
            } while(error==LogError::BufferFull && !failed);
            if(error!=LogError::None) failed=true;
            const auto state=writer.snapshot();
            if(state.accepted!=state.saved+state.buffered || state.buffered>64) failed=true;
        }
        done=true;
    });
    while(!done || writer.snapshot().buffered) {
        const auto state=writer.snapshot();
        if(state.buffered>=16 || (done && state.buffered)) {
            if(!writer.flush(5000)) { failed=true; break; }
        } else std::this_thread::yield();
    }
    producer.join();
    check(!failed,"parallel producer and consumer keep coherent counters");
    check(writer.requestFlush(123,total,true)==LogError::None && writer.flush(6000),"parallel stream closes");
    const auto file=read(path+"/"+writer.snapshot().name);
    check(file.size()==512+total*32,"parallel stream saves every accepted record");
    for(unsigned n=1;n<=total;++n) {
        uint8_t expected[32];
        check(encodeLogRecord(point(n).fix,expected) && std::memcmp(file.data()+512+(n-1)*32,expected,32)==0,
              "parallel stream preserves exact record order and CRC through many ring wraps");
    }
}
}
void testLogRing() {
    char directory[]="/tmp/geoputer-ring-XXXXXX";
    check(mkdtemp(directory)!=nullptr,"temporary ring storage");
    try { concurrentAndRetry(std::string(directory)+"/retry"); parallelStream(std::string(directory)+"/parallel"); }
    catch(...) { std::filesystem::remove_all(directory); throw; }
    std::filesystem::remove_all(directory);
}

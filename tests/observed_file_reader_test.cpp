#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

#include "application/task/storage/galuchat/ObservedFileReader.h"

namespace {
constexpr size_t Buffer = geoputer::config::FileReadBufferBytes;
constexpr size_t Threshold = geoputer::config::FileReadThroughSkipBytes;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Call> void expectThrow(Call call) {
    bool failed = false;
    try { call(); } catch (const std::runtime_error&) { failed = true; }
    require(failed, "expected an I/O/bounds error");
}

struct File {
    std::string path;
    std::vector<uint8_t> bytes;
    explicit File(size_t size) : bytes(size) {
        for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(i * 131 + (i >> 8) * 17);
        char name[] = "/tmp/reversegeoputer-reader-XXXXXX";
        int fd = mkstemp(name);
        require(fd >= 0, "temporary file creation failed");
        path = name;
        std::FILE* file = fdopen(fd, "wb");
        if (!file) { close(fd); std::remove(path.c_str()); throw std::runtime_error("fdopen failed"); }
        const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
        const int closed = std::fclose(file);
        require(written == bytes.size() && closed == 0, "fixture write failed");
    }
    ~File() { std::remove(path.c_str()); }
};
struct Observer : geoputer::StorageObserver {
    unsigned begins = 0, ends = 0;
    int depth = 0;
    void sdBegin() override { ++begins; ++depth; }
    void sdEnd() override { ++ends; --depth; }
    void cacheState(geoputer::RgCacheState, bool = false) override {}
    void balanced() const { require(depth == 0 && begins == ends, "unbalanced SD activity"); }
};

void bufferedAndShort(const File& file) {
    geoputer::FileIoTiming io{};
    Observer observer;
    {
        geoputer::ObservedFileReader reader(file.path, file.bytes.size(), 0, &observer, &io);
        io = {};
        auto begins = observer.begins;
        reader.skipInByte(0);
        require(io.reads == 0 && io.seeks == 0 && reader.pos() == 0 && observer.begins == begins,
                "zero skip performed I/O");
        require(reader.readByte() == file.bytes[0], "initial byte");
        io = {};
        begins = observer.begins;
        reader.skipInByte(100);
        require(reader.readByte() == file.bytes[101], "buffered skip byte");
        require(io.reads == 0 && io.seeks == 0 && observer.begins == begins, "buffered skip performed I/O");
    }
    observer.balanced();
    if (Threshold == 0) return;
    io = {};
    {
        geoputer::ObservedFileReader reader(file.path, file.bytes.size(), 0, &observer, &io);
        (void)reader.readByte();
        io = {};
        // Skip the old buffer tail and one byte from the next refill.
        reader.skipInByte(Buffer);
        require(reader.pos() == Buffer + 1, "short skip position");
        require(io.reads == 1 && io.bytes == Buffer && io.seeks == 0, "short gap used fseek");
        require(reader.readByte() == file.bytes[Buffer + 1], "short skip byte");
        require(io.reads == 1 && io.seeks == 0, "read-ahead was discarded");
    }
    observer.balanced();
}

void thresholdBoundary(const File& file) {
    for (bool over : {false, true}) {
        geoputer::FileIoTiming io{};
        geoputer::ObservedFileReader reader(file.path, file.bytes.size(), 0, nullptr, &io);
        (void)reader.readByte();
        io = {};
        const size_t gap = Threshold + static_cast<size_t>(over);
        const size_t target = Buffer + gap;
        // Total length exceeds the threshold; only the unbuffered gap matters.
        reader.skipInByte(Buffer - 1 + gap);
        require(reader.pos() == target, "threshold skip position");
        if (over) require(io.reads == 0 && io.seeks == 1, "long skip did not seek");
        else require(io.seeks == 0 && io.reads == (gap + Buffer - 1) / Buffer,
                     "inclusive threshold did not read through");
        require(reader.readByte() == file.bytes[target], "threshold skip byte");
    }
    geoputer::FileIoTiming io{};
    geoputer::ObservedFileReader reader(file.path, file.bytes.size(), 37, nullptr, &io);
    io = {};
    reader.skipInByte(Threshold + 1);
    require(io.seeks == 1 && io.reads == 0, "initial long skip did not seek");
    require(reader.readByte() == file.bytes[37 + Threshold + 1], "nonzero file origin");
}

void endAndTruncation() {
    File short_file(19);
    Observer observer;
    geoputer::FileIoTiming io{};
    {
        geoputer::ObservedFileReader reader(short_file.path, short_file.bytes.size(), 3, &observer, &io);
        io = {};
        reader.skipInByte(16);
        require(reader.pos() == 16 && reader.atEnd(), "EOF skip position");
        if (Threshold >= 16) require(io.reads == 1 && io.bytes == 16 && io.seeks == 0, "short EOF refill");
        io = {};
        reader.skipInByte(0);
        expectThrow([&] { reader.readByte(); });
        expectThrow([&] { reader.skipInByte(1); });
        require(reader.atEnd() && io.reads == 0 && io.seeks == 0, "EOF access performed I/O");
    }
    observer.balanced();
    if (Threshold < 32) return;
    // Declared length is larger than the physical file, simulating truncation.
    io = {};
    {
        geoputer::ObservedFileReader reader(short_file.path, 128, 0, &observer, &io);
        io = {};
        expectThrow([&] { reader.skipInByte(32); });
        require(reader.pos() == 19 && io.reads == 2 && io.bytes == 19 && io.seeks == 0,
                "truncated skip silently succeeded / incorrect consumed position");
        observer.balanced();
    }
    observer.balanced();
}

void outsideBounds(const File& file) {
    geoputer::FileIoTiming io{};
    geoputer::ObservedFileReader reader(file.path, file.bytes.size(), 0, nullptr, &io);
    (void)reader.readByte();
    io = {};
    expectThrow([&] { reader.skipInByte(file.bytes.size()); });
    require(reader.pos() == 1 && io.reads == 0 && io.seeks == 0, "invalid skip changed position or performed I/O");
    require(reader.readByte() == file.bytes[1], "invalid skip discarded the buffer");
}

void readProfiles(const File& file) {
    geoputer::FileIoTiming io{};
    Observer observer;
    {
        geoputer::ObservedFileReaderFactory factory(file.path, &observer, &io, {4096,4096});
        auto small = factory.create();
        factory.setReadOptions({8192,4096});
        auto medium = factory.create();
        factory.setReadOptions({16384,4096});
        auto large = factory.create();
        for (const auto& sample : {std::make_pair(small.get(),4096u),std::make_pair(medium.get(),8192u),std::make_pair(large.get(),16384u)}) {
            io={};
            require(sample.first->readByte()==file.bytes[0], "profile initial byte");
            require(io.reads==1 && io.bytes==sample.second, "factory changed an existing Reader's buffer");
        }
        io={}; small->skipInByte(4095);
        require(small->readByte()==file.bytes[4096] && io.bytes==4096, "small Reader was resized by later profiles");
        bool invalid=false;
        try { factory.setReadOptions({0,4096}); } catch (const std::invalid_argument&) { invalid=true; }
        require(invalid,"zero buffer accepted");
        auto unchanged=factory.create();io={};(void)unchanged->readByte();
        require(io.bytes==16384,"invalid profile replaced the valid factory options");
    }
    observer.balanced();
    for (const size_t bytes : {4096u,8192u,16384u}) {
        for (const size_t gap : {4096u,4097u}) {
            geoputer::ObservedFileReader reader(file.path,file.bytes.size(),0,nullptr,&io,{bytes,4096});
            (void)reader.readByte();io={};
            reader.skipInByte(bytes-1+gap);
            require(io.seeks==(gap>4096?1u:0u) && io.reads==(gap<=4096?1u:0u),
                    "read-through threshold changed with buffer capacity");
            require(reader.readByte()==file.bytes[bytes+gap],"profile threshold position");
        }
        geoputer::ObservedFileReader seek_only(file.path,file.bytes.size(),0,nullptr,&io,{bytes,0});
        (void)seek_only.readByte();io={};seek_only.skipInByte(bytes);
        require(io.seeks==1 && io.reads==0,"zero threshold did not disable read-through for a profile");
    }
    io={};
    bool invalid=false;
    try { geoputer::ObservedFileReader bad(file.path,file.bytes.size(),0,&observer,&io,{0,4096}); }
    catch (const std::invalid_argument&) {invalid=true;}
    require(invalid && io.opens==0,"zero buffer opened a file or was accepted");
    observer.balanced();
}

void boundedRanges(const File& file) {
    geoputer::FileIoTiming io{};
    Observer observer;
    geoputer::ObservedFileReaderFactory factory(file.path,&observer,&io,{16384,4096});
    for (const size_t length : {1u,2u,123u,4095u,4096u,4097u,16383u,16384u,16385u}) {
        io={};
        auto reader=factory.createRange(73,length);
        require(reader->readByte()==file.bytes[73],"range origin");
        require(io.bytes==std::min<size_t>(16384,length),"range over-read or wrong refill size");
        reader->skipInByte(length-1);
        require(reader->atEnd() && reader->pos()==length && io.bytes==length,"range refill exceeded its end");
        const auto reads=io.reads;expectThrow([&]{reader->readByte();});expectThrow([&]{reader->skipInByte(1);});
        require(io.reads==reads,"range end attempted I/O");
    }
    {
        const size_t length=32768+37;
        auto reader=factory.createRange(73,length);io={};
        (void)reader->readByte();reader->skipInByte(length-2);
        require(reader->readByte()==file.bytes[73+length-1] && reader->atEnd() && io.reads==2 && io.bytes==16385 && io.seeks==1,
                "long range seek or final bounded refill");
    }
    io={};
    auto empty=factory.createRange(file.bytes.size(),0);
    require(empty->atEnd() && io.opens==0 && io.reads==0,"empty file range allocated/opened storage");
    expectThrow([&]{empty->readByte();});
    expectThrow([&]{factory.createRange(file.bytes.size(),1);});
    expectThrow([&]{factory.createRange(file.bytes.size()+1,0);});
    expectThrow([&]{factory.createRange(1,std::numeric_limits<size_t>::max());});
    require(io.opens==0 && io.reads==0,"invalid file range attempted I/O");
    observer.balanced();
}

void mixedBitsAndSkips(const File& file) {
    uint32_t random = 0x12345678;
    auto next = [&] { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    for (size_t trial = 0; trial < 8; ++trial) {
        const size_t origin = trial * 37;
        geoputer::ObservedFileReader actual(file.path, file.bytes.size(), origin, nullptr);
        galuchat::BufferReader expected(file.bytes.data() + origin, file.bytes.size() - origin);
        for (int step = 0; step < 1000; ++step) {
            const size_t bits = expected.remaining() * 8 + expected.bitOffset();
            if (bits == 0) break;
            switch (next() % 4) {
            case 0: {
                const int count = static_cast<int>(std::min<size_t>(1 + next() % 31, bits));
                require(actual.readBitsAsInt32(count) == expected.readBitsAsInt32(count), "bit-read mismatch");
                break;
            }
            case 1: {
                const size_t count = std::min<size_t>(next() % (Buffer * 3), bits / 8);
                actual.skipInByte(count); expected.skipInByte(count);
                break;
            }
            case 2: {
                const size_t count = std::min<size_t>(next() % (Buffer * 16), bits);
                actual.skipBits(count); expected.skipBits(count);
                break;
            }
            default: actual.skipToByte(); expected.skipToByte(); break;
            }
            require(actual.pos() == expected.pos() && actual.bitOffset() == expected.bitOffset()
                    && actual.atEnd() == expected.atEnd(), "stream state mismatch after mixed operations");
        }
    }
}
} // namespace

int main() {
    File file(std::max<size_t>(128 * Buffer, 4 * Threshold + 4 * Buffer + 100));
    bufferedAndShort(file);
    thresholdBoundary(file);
    endAndTruncation();
    outsideBounds(file);
    mixedBitsAndSkips(file);
    readProfiles(file);
    boundedRanges(file);
    std::cout << "ObservedFileReader: buffer/threshold boundaries, read-ahead, seeks, EOF/errors, activity, bit-stream equivalence, per-Reader profiles, independent thresholds and bounded file I/O OK\n";
}

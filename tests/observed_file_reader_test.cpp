#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <iostream>
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
    std::cout << "ObservedFileReader: buffer/threshold boundaries, read-ahead, seeks, EOF/errors, activity, bit-stream equivalence OK\n";
}

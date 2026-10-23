#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include "application/task/storage/galuchat/CachedMapReader.h"
#include "support/MapIndexFixture.h"
#include "application/task/storage/galuchat/ObservedFileReader.h"

namespace {
using Bytes = std::vector<uint8_t>;

void require(bool condition, const char* label) {
    if (!condition) throw std::runtime_error(label);
}

void mbUInt(Bytes& out, size_t value) {
    if (value < 252) out.push_back(static_cast<uint8_t>(value));
    else if (value < 508) {
        out.push_back(255); out.push_back(static_cast<uint8_t>(value - 252));
    } else {
        require(value < 66044, "fixture MBUInt too large");
        value -= 508;
        out.push_back(254); out.push_back(static_cast<uint8_t>(value >> 8));
        out.push_back(static_cast<uint8_t>(value));
    }
}
void append(Bytes& out, const Bytes& bytes) { out.insert(out.end(), bytes.begin(), bytes.end()); }
Bytes chunk(const char* tag, const Bytes& payload) {
    Bytes out(tag, tag + 4);
    mbUInt(out, payload.size()); append(out, payload);
    return out;
}
Bytes image(uint8_t base) {
    Bytes payload;
    mbUInt(payload, 256); mbUInt(payload, 256); payload.push_back(7);
    for (uint8_t index = 0; index < 4; ++index) {
        // Unsized block, mono container, update palette slot zero.
        append(payload, {0, 0x28, static_cast<uint8_t>(base + index)});
    }
    return chunk("GI01", payload);
}
Bytes dataset(size_t maps = 1, uint8_t first = 1) {
    Bytes payload(16, 0);
    const std::string format = "WGSMapSet/3";
    std::copy(format.begin(), format.end(), payload.begin());
    append(payload, {1, 1, 0}); mbUInt(payload, maps);
    for (size_t i = 0; i < maps; ++i) append(payload, {0, 0});
    Bytes out = chunk("GLCH", payload);
    for (size_t i = 0; i < maps; ++i) append(out, image(i == 0 ? first : 10));
    return out;
}

struct Access { size_t end = 0; };
class Factory final : public galuchat::ReaderFactory {
public:
    explicit Factory(const Bytes& bytes) : bytes_(bytes) {}
    std::unique_ptr<galuchat::ByteReader> create(size_t offset = 0) const override {
        require(offset <= bytes_.size(), "factory offset");
        auto access = std::make_shared<Access>();
        accesses.push_back(access);
        class Reader final : public galuchat::ByteReader {
        public:
            Reader(const Factory& factory, size_t offset, std::shared_ptr<Access> access)
                : factory_(factory), offset_(offset), access_(std::move(access)) {}
            size_t pos() const override { return access_->end; }
            bool atEnd() const override { return offset_ + pos() == factory_.bytes_.size(); }
        protected:
            uint8_t nextByte() override {
                require(!atEnd(), "fixture EOF");
                if (pos() == factory_.fail_at) throw std::runtime_error("injected SD read error");
                return factory_.bytes_[offset_ + access_->end++];
            }
        private:
            const Factory& factory_;
            size_t offset_;
            std::shared_ptr<Access> access_;
        };
        return std::make_unique<Reader>(*this, offset, access);
    }
    size_t fail_at = std::numeric_limits<size_t>::max();
    mutable std::vector<std::shared_ptr<Access>> accesses;
private:
    const Bytes& bytes_;
};
class Observer final : public geoputer::StorageObserver {
public:
    void sdBegin() override {}
    void sdEnd() override {}
    void cacheState(geoputer::RgCacheState next, bool = false) override { state = next; }
    geoputer::RgCacheState state = geoputer::RgCacheState::Unprepared;
};

void syntheticChecks() {
    Bytes bytes = dataset();
    auto factory = std::make_shared<Factory>(bytes);
    Observer observer;
    auto cache=test_index::cache(factory,bytes,&observer);
    require(cache.chunkCount() == 1 && cache.stats().bytes == 0, "lazy cache allocation");
    factory->accesses.clear();
    require(cache.readWgsPoint(64,64) == 1, "first cached point");
    require(factory->accesses.size() == 1 && factory->accesses[0]->end == 13,
            "cache rebuild traversed the unused GI01 tail");
    require(cache.stats().bytes == 65536 && cache.stats().valid
            && observer.state == geoputer::RgCacheState::Ready, "cache allocation / RG ready");
    factory->accesses.clear();
    require(cache.readWgsPoint(127,127) == 1 && factory->accesses.empty(), "cache hit performed I/O");
    require(cache.stats().fills == 1 && cache.stats().hits == 1, "cache hit counters");
    // A failed second block leaves a partially updated image, which must stay invalid.
    factory->fail_at = 15;
    bool failed = false;
    try { cache.readWgsPoint(128,64); } catch (const std::runtime_error&) { failed = true; }
    require(failed && !cache.stats().valid && cache.stats().failures == 1
            && observer.state == geoputer::RgCacheState::Unprepared, "partial cache marked valid");
    factory->fail_at = std::numeric_limits<size_t>::max();
    require(cache.readWgsPoint(128,64) == 2 && cache.stats().valid, "cache did not recover");
    require(cache.readWgsPoint(128,128) == 4, "cache boundary pixels");
    cache.invalidate();
    require(!cache.isCached(128,128) && observer.state == geoputer::RgCacheState::Unprepared,
            "cache invalidation");
    require(cache.readWgsPoint(400,400) == 0 && cache.stats().valid, "outside coverage / code zero");

    // Preserve point lookup priority: first nonzero map wins, not last nonzero map.
    for (uint8_t first : {uint8_t{0}, uint8_t{1}}) {
        Bytes overlap = dataset(2, first);
        auto overlapping = std::make_shared<galuchat::BufferReaderFactory>(overlap);
        auto cached=test_index::cache(overlapping,overlap);
        require(cached.readWgsPoint(64,64) == (first == 0 ? 10 : 1), "overlap point priority");
    }
    Bytes at_limit = dataset(geoputer::config::MaxDatasetChunks);
    auto accepted=test_index::cache(std::make_shared<galuchat::BufferReaderFactory>(at_limit),at_limit);
    require(accepted.chunkCount() == geoputer::config::MaxDatasetChunks, "inclusive chunk limit");
    Bytes too_many = dataset(geoputer::config::MaxDatasetChunks + 1);
    bool rejected = false;
    try {
        auto invalid=test_index::cache(std::make_shared<galuchat::BufferReaderFactory>(too_many),too_many);
    } catch (const geoputer::MapChunkLimitExceeded& error) {
        require(error.count == geoputer::config::MaxDatasetChunks + 1
                && error.limit == geoputer::config::MaxDatasetChunks, "application limit details");
        rejected = true;
    }
    require(rejected, "application exceeded configured map count");
    std::cout << "cache integration: early stop, hits, boundaries, failure/recovery, priority, limits OK\n";
}

void realDataChecks(const char* path) {
    geoputer::FileIoTiming timing{};
    auto factory = std::make_shared<geoputer::ObservedFileReaderFactory>(path, nullptr, &timing);
    auto cache=test_index::cacheFile(factory,path);
    galuchat::GaluchatWGSMapSet3Reader reference(factory);
    for (const auto& point : std::vector<galuchat::DoubleRect>{
            {140.027194,35.681641,0,0}, {140.027194,35.681641,0,0},
            {140.0200,35.6800,0,0}, {140.0300,35.6900,0,0}, {0,0,0,0}}) {
        int64_t expected = 0;
        if (!reference.readWgsPoint(point.x, point.y, expected)) expected = 0;
        timing = {};
        require(cache.readWgsPoint(point.x, point.y) == expected, "real-data point differs");
        timing = {};
        const uint32_t fills = cache.stats().fills;
        require(cache.readWgsPoint(point.x, point.y) == expected, "real-data cached point differs");
        require(cache.stats().fills == fills && timing.bytes == 0 && timing.opens == 0
                && timing.reads == 0 && timing.seeks == 0, "real-data hit performed SD I/O");
    }
    std::cout << "real data: " << cache.chunkCount() << " chunks, 5 points match SDK point API; hits have no I/O\n";
}
} // namespace

int main(int argc, char** argv) {
    syntheticChecks();
    for (int i = 1; i < argc; ++i) realDataChecks(argv[i]);
}

#include <array>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <vector>
#include <galuchat/std_file_reader.hpp>
#include "support/MapIndexFixture.h"
#include "task/storage/galuchat/CachedMapReader.h"

using namespace geoputer;
namespace {
using Bytes = std::vector<uint8_t>;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

// Tiny valid WGSMapSet fixtures cover overlaps, zeros and values near uint32's
// limit independently of the geographic fixtures used by the file tests.
void mbUInt(Bytes& out, uint64_t value) {
    if (value < 252) { out.push_back(static_cast<uint8_t>(value)); return; }
    unsigned count; uint64_t base;
    if (value < 508) { out.push_back(255); count = 1; base = 252; }
    else if (value < 66044) { out.push_back(254); count = 2; base = 508; }
    else if (value < 16843260) { out.push_back(253); count = 3; base = 66044; }
    else { out.push_back(252); count = 4; base = 16843260; }
    value -= base;
    while (count--) out.push_back(static_cast<uint8_t>(value >> (count * 8)));
}
Bytes chunk(const char* tag, const Bytes& payload) {
    Bytes result(tag, tag + 4);
    mbUInt(result, payload.size());
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}
void mbPositiveInt(Bytes& out, uint64_t value) {
    if (value < 124) { out.push_back(static_cast<uint8_t>(value)); return; }
    unsigned count; uint64_t base;
    if (value < 380) { out.push_back(127); count = 1; base = 124; }
    else if (value < 65916) { out.push_back(126); count = 2; base = 380; }
    else { out.push_back(125); count = 3; base = 65916; }
    value -= base;
    while (count--) out.push_back(static_cast<uint8_t>(value >> (count * 8)));
}
Bytes mapset(const std::vector<std::array<uint64_t, 4>>& layers, bool separated = false) {
    const std::string version("WGSMapSet/3");
    Bytes header(version.begin(), version.end());
    header.resize(16);
    header.insert(header.end(), {10, 20, 0});  // X/Y unitInv and empty metadata.
    mbUInt(header, layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        mbPositiveInt(header, 100 + (separated ? i * 1000 : 0));
        mbPositiveInt(header, 100);
    }
    Bytes result = chunk("GLCH", header);
    for (const auto& values : layers) {
        Bytes payload{2, 2, 1, 0, 0x70};  // 2x2 GI01, raw 2x2 block, direct values.
        for (const auto value : values) mbUInt(payload, value);
        const auto image = chunk("GI01", payload);
        result.insert(result.end(), image.begin(), image.end());
    }
    return result;
}

class BudgetReader final : public galuchat::ByteReader {
public:
    BudgetReader(std::unique_ptr<galuchat::ByteReader> reader, size_t budget)
        : reader_(std::move(reader)), budget_(budget) {}
    size_t pos() const override { return reader_->pos(); }
    bool atEnd() const override { return reader_->atEnd(); }
protected:
    uint8_t nextByte() override {
        if (!budget_) throw std::runtime_error("injected read failure");
        --budget_; return reader_->readByte();
    }
    void skipByte(size_t length) override {
        if (length > budget_) throw std::runtime_error("injected seek failure");
        budget_ -= length; reader_->skipInByte(length);
    }
private:
    std::unique_ptr<galuchat::ByteReader> reader_;
    size_t budget_;
};

class CountingFactory final : public galuchat::ReaderFactory {
public:
    explicit CountingFactory(const Bytes& bytes) : source_(std::make_shared<galuchat::BufferReaderFactory>(bytes)) {}
    explicit CountingFactory(const std::string& path) : source_(std::make_shared<galuchat::FileReaderFactory>(path, 4096)) {}
    std::unique_ptr<galuchat::ByteReader> create(size_t offset = 0) const override {
        ++creates;
        auto reader = source_->create(offset);
        if (fail_reads || read_budget != SIZE_MAX)
            return std::make_unique<BudgetReader>(std::move(reader), fail_reads ? 12 : read_budget);
        return reader;
    }
    mutable unsigned creates = 0;
    bool fail_reads = false;
    size_t read_budget = SIZE_MAX;
private:
    std::shared_ptr<const galuchat::ReaderFactory> source_;
};

template<class Function>
void throws(Function operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
}

std::vector<uint8_t> oversizedMapFixture(unsigned count) {
    return mapset(std::vector<std::array<uint64_t, 4>>(count, {1, 2, 3, 4}));
}

void testMapCache() {
    const auto at_limit = mapset(std::vector<std::array<uint64_t, 4>>(config::MaxDatasetChunks, {1, 2, 3, 4}), true);
    auto indexed_source = std::make_shared<CountingFactory>(at_limit);
    geoputer::CodeCacheBuffer indexed_pixels{};auto indexed=test_index::cache(indexed_pixels,indexed_source,at_limit);
    check(indexed.chunkCount() == config::MaxDatasetChunks && indexed.stats().bytes == 0,
          "exactly the configured maximum chunks are accepted without allocating the image");
    const auto indexed_before = indexed_source->creates;
    check(indexed.readWgsPoint(10, 5) == 1 && indexed_source->creates == indexed_before + 1,
          "indexed rectangle opens only its intersecting chunk among all indexed chunks");
    const auto indexed_hit = indexed_source->creates;
    check(indexed.readWgsPoint(-30, 5) == 0 && indexed_source->creates == indexed_hit,
          "a rectangle outside all indexed chunks performs no map I/O");

    const auto over_limit = mapset(std::vector<std::array<uint64_t, 4>>(config::MaxDatasetChunks + 1, {1, 2, 3, 4}));
    auto rejected_source = std::make_shared<CountingFactory>(over_limit);
    auto header = galuchat::BufferReaderFactory(over_limit).create();
    header->skipInByte(4); header->readMbUInt(); header->skipInByte(16);
    header->readMbUInt(); header->readMbUInt(); header->readMbUInt(); header->readMbUInt();
    rejected_source->read_budget = header->pos();
    bool rejected_limit = false;
    try { geoputer::CodeCacheBuffer rejected_pixels{};auto rejected=test_index::cache(rejected_pixels,rejected_source,over_limit); }
    catch (const MapChunkLimitExceeded& error) {
        rejected_limit = error.count == config::MaxDatasetChunks + 1 && error.limit == config::MaxDatasetChunks;
    }
    check(rejected_limit && rejected_source->creates == 0,
          "max + 1 chunks are rejected before reading origins or any GI01 header/body");
    const auto bytes = mapset({{1, 2, 3, 4}});
    auto factory = std::make_shared<CountingFactory>(bytes);
    geoputer::CodeCacheBuffer cache_pixels{};auto cache=test_index::cache(cache_pixels,factory,bytes);
    check(cache.stats().bytes == 0 && !cache.stats().valid, "header preparation leaves the fixed image invalid");
    const auto before = factory->creates;
    check(!cache.isCached(10, 5) && factory->creates == before && cache.stats().bytes == 0,
          "access-mode prediction on a cold cache does not read or allocate");
    check(cache.readWgsPoint(10, 5) == 1, "first point loads its centered rectangle");
    check(factory->creates == before + 1 && cache.stats().fills == 1 && cache.stats().bytes == 65536,
          "one miss reads one image into a 64 KiB buffer");
    check(cache.stats().bounds.x == 36 && cache.stats().bounds.y == 36, "center is local cell 64,64");
    const auto filled = factory->creates;
    check(cache.isCached(10, 5) && cache.isCached(3.6, 1.8) && cache.isCached(16.3, 8.15) &&
          !cache.isCached(16.4, 5) && !cache.isCached(10, 8.2) &&
          factory->creates == filled && cache.stats().hits == 0,
          "access-mode prediction uses exact cache bounds without reading or changing statistics");
    check(cache.readWgsPoint(10.1, 5) == 2 && cache.readWgsPoint(10, 5.05) == 3 && cache.readWgsPoint(10.1, 5.05) == 4,
          "grid orientation and x/y resolution match the point reader");
    check(cache.readWgsPoint(3.6, 1.8) == 0 && cache.readWgsPoint(16.3, 8.15) == 0,
          "inclusive lower bounds and last upper cells are cache hits, including zero");
    check(factory->creates == filled, "in-range map lookups create no file/read sessions");
    check(cache.readWgsPoint(16.4, 5) == 0 && factory->creates == filled + 1,
          "exclusive east boundary triggers a centered refill");
    const auto xfill = factory->creates;
    check(cache.readWgsPoint(16.4, 8.2) == 0 && factory->creates == xfill + 1,
          "exclusive north boundary triggers a centered refill");
    cache.invalidate();
    check(!cache.isCached(10, 5), "invalidated cache predicts a reload");
    check(!cache.stats().valid && cache.stats().bytes == 65536, "invalidation retains the single allocation");
    const auto invalidated = factory->creates;
    cache.readWgsPoint(10, 5);
    check(factory->creates == invalidated + 1, "explicit invalidation forces a new image");

    factory->fail_reads = true;
    check(cache.readWgsPoint(10, 5) == 1, "a hit remains usable without access to the map source");
    // This east-edge refill includes the fixture at its west edge: the 12-byte
    // budget fails after two nonzero cells have actually been written.
    throws([&] { cache.readWgsPoint(16.4, 5); }, "refill propagates read failure");
    check(!cache.stats().valid && cache.stats().failures == 1, "failed refill never publishes partial data");
    throws([&] { cache.readWgsPoint(10, 5); }, "old bounds cannot expose a partially overwritten image");
    factory->fail_reads = false;
    check(cache.readWgsPoint(10, 5) == 1 && cache.stats().valid, "a later successful request recovers from failure");
    cache.invalidate();
    cache.readWgsPoint(-0.05, -0.025);
    check(cache.stats().bounds.x == -65 && cache.stats().bounds.y == -65,
          "negative half-grid coordinates round away from zero like the SDK");
    const auto negative = factory->creates;
    check(cache.readWgsPoint(-0.05, -0.025) == 0 && factory->creates == negative, "undefined regions are cached");
    throws([&] { cache.readWgsPoint(std::numeric_limits<double>::infinity(), 0); }, "nonfinite coordinates are rejected");

    const auto overlaps = mapset({{1, 0, 3, 4}, {2, 2, 2, 2}});
    galuchat::GaluchatWGSMapSet3Reader raw(overlaps);
    geoputer::CodeCacheBuffer overlapping_pixels{};auto overlapping=test_index::cache(overlapping_pixels,std::make_shared<galuchat::BufferReaderFactory>(overlaps),overlaps);
    uint32_t rect[4]{};
    galuchat::RasterView<uint32_t> raster(2, 2, rect);
    raw.readWgsRect({100, 100, 2, 2}, raster);
    check(rect[0] == 2, "fixture exercises SDK rectangle overwrite order");
    for (int y = 100; y <= 101; ++y) for (int x = 100; x <= 101; ++x) {
        int64_t point = 0;
        check(raw.readWgsPoint(x, y, point), "overlapping fixture point exists");
        check(overlapping.readWgsPoint(x / 10.0, y / 20.0) == point,
              "cache preserves first-nonzero point priority and transparent zeros");
    }
    const auto limit = mapset({{UINT32_MAX, 0, 0, 0}});
    geoputer::CodeCacheBuffer full_pixels{};auto full=test_index::cache(full_pixels,std::make_shared<galuchat::BufferReaderFactory>(limit),limit);
    check(full.readWgsPoint(10, 5) == UINT32_MAX, "all uint32 code bits are preserved");
    const auto overflow = mapset({{UINT64_C(1) << 32, 0, 0, 0}});
    geoputer::CodeCacheBuffer tooLarge_pixels{};auto tooLarge=test_index::cache(tooLarge_pixels,std::make_shared<galuchat::BufferReaderFactory>(overflow),overflow);
    throws([&] { tooLarge.readWgsPoint(10, 5); }, "out-of-range codes cannot silently truncate");
    check(!tooLarge.stats().valid, "code overflow leaves no valid cache");
    std::cout << "PASS: cache boundaries, zero hits/no map I/O, invalidation, partial read failures, overlaps and uint32 range\n";
}

void testMapCacheFile(const std::string& path, const std::string& book, const std::string& temporary) {
    namespace fs = std::filesystem;
    const auto directory = fs::path(temporary) / "saved-index-cache";
    fs::create_directories(directory);
    const auto map = directory / "map.glc", dictionary = directory / "book.giswordbook";
    fs::copy_file(path, map, fs::copy_options::overwrite_existing);
    fs::copy_file(book, dictionary, fs::copy_options::overwrite_existing);
    test_index::generate(directory.string(), "map.glc", "book.giswordbook");
    const auto index_path = (directory / "_mapindex.dat").string();
    auto source = std::make_shared<CountingFactory>(map.string());
    auto index = std::make_shared<CountingFactory>(index_path);
    geoputer::CodeCacheBuffer cache_pixels{};geoputer::CachedMapReader cache(cache_pixels,geoputer::DatasetIndex{}.loadMap(index_path, map.string()), source, index);
    galuchat::GaluchatWGSMapSet3Reader point(std::make_shared<galuchat::FileReaderFactory>(path, 4096));
    const std::array<std::pair<double, double>, 4> centers{{
        {140.027194, 35.681641}, {139.7528, 35.6852}, {140.0, 35.0}, {-0.2, -0.1}}};
    unsigned checked = 0;
    for (const auto center : centers) {
        cache.readWgsPoint(center.first, center.second);
        const auto rect = cache.stats().bounds;
        const auto sessions = source->creates, index_sessions = index->creates;
        for (const int y : {0, 1, 31, 63, 64, 65, 96, 126, 127}) for (const int x : {0, 1, 31, 63, 64, 65, 96, 126, 127}) {
            const int ix = rect.x + x, iy = rect.y + y;
            int64_t expected = 0;
            if (!point.readWgsPoint(ix, iy, expected)) expected = 0;
            check(cache.readWgsPoint(ix / static_cast<double>(point.unit_inv_x), iy / static_cast<double>(point.unit_inv_y)) == expected,
                  "real cached code must equal original SDK point code");
            ++checked;
        }
        check(source->creates == sessions && index->creates == index_sessions,
              "real cache hits open neither map nor saved index");
    }
    std::cout << "PASS: " << checked << " real cached/point comparisons; unitInv=" << point.unit_inv_x << ',' << point.unit_inv_y << '\n';
}

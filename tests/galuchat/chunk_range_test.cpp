#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
#include "galuchat/wgsmapset3_indexed_reader.hpp"

namespace {
using Bytes = std::vector<uint8_t>;
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
template<class Call> void rejects(Call call) {
    bool failed = false;
    try { call(); } catch (const std::runtime_error&) { failed = true; }
    require(failed, "expected range/data error");
}
void mbUInt(Bytes& out, size_t value) {
    if (value < 252) out.push_back(static_cast<uint8_t>(value));
    else if (value < 508) { out.push_back(255); out.push_back(static_cast<uint8_t>(value - 252)); }
    else {
        require(value < 66044, "fixture too large"); value -= 508;
        out.push_back(254); out.push_back(static_cast<uint8_t>(value >> 8)); out.push_back(static_cast<uint8_t>(value));
    }
}
void append(Bytes& out, const Bytes& part) { out.insert(out.end(), part.begin(), part.end()); }
Bytes chunk(const char* tag, const Bytes& payload) {
    Bytes out(tag, tag + 4); mbUInt(out, payload.size()); append(out, payload); return out;
}
Bytes image(unsigned dimension, uint8_t code) {
    Bytes payload; mbUInt(payload, dimension); mbUInt(payload, dimension); payload.push_back(1);
    for (unsigned i = 0; i < (dimension / 2) * (dimension / 2); ++i) append(payload, {0, 0x28, code});
    return chunk("GI01", payload);
}
struct Fixture {
    std::vector<Bytes> images{image(2,3), image(64,7), image(4,11)};
    std::vector<unsigned> west{20,40,0}; // File order deliberately differs from spatial order.
    Bytes bytes;
    std::vector<size_t> offsets;
    explicit Fixture(bool layout) { rebuild(layout); }
    void rebuild(bool layout) {
        Bytes payload(16,0); const std::string version="WGSMapSet/3";
        std::copy(version.begin(),version.end(),payload.begin()); append(payload,{1,1,0,3});
        for (auto x : west) append(payload,{static_cast<uint8_t>(x),0});
        bytes=chunk("GLCH",payload);
        if (layout) append(bytes,chunk("LAYO",{1,2,3,4}));
        offsets.clear();
        for (const auto& part : images) { offsets.push_back(bytes.size()); append(bytes,part); }
    }
};
// Compiles with the old, single-method factory contract.
class LegacyFactory : public galuchat::ReaderFactory {
public:
    explicit LegacyFactory(const Bytes& bytes) : bytes_(bytes) {}
    std::unique_ptr<galuchat::ByteReader> create(size_t offset=0) const override {
        ++creates;
        return galuchat::BufferReaderFactory(bytes_).create(offset);
    }
    mutable unsigned creates=0;
protected:
    const Bytes& bytes_;
};
class TracedFactory final : public LegacyFactory {
public:
    using LegacyFactory::LegacyFactory;
    struct Range {size_t offset,length;};
    std::unique_ptr<galuchat::ByteReader> createRange(size_t offset,size_t length) const override {
        ranges.push_back({offset,length});
        return galuchat::BufferReaderFactory(bytes_).createRange(offset,length);
    }
    mutable std::vector<Range> ranges;
};
void checkFactories() {
    Bytes bytes{0,1,2,3,4,5,6,7,8,9};
    galuchat::BufferReaderFactory buffer(bytes);
    LegacyFactory legacy(bytes);
    for (const galuchat::ReaderFactory* factory : {static_cast<const galuchat::ReaderFactory*>(&buffer),static_cast<const galuchat::ReaderFactory*>(&legacy)}) {
        auto reader=factory->createRange(3,4);
        require(reader->pos()==0 && reader->readBitsAsInt32(4)==0, "range starts at wrong byte/bit");
        require(reader->readBitsAsInt32(4)==3, "range byte content");
        reader->skipInByte(2); require(reader->readByte()==6 && reader->atEnd(), "range end");
        rejects([&]{reader->readByte();}); rejects([&]{reader->skipInByte(1);});
        reader->skipInByte(0);
        auto empty=factory->createRange(bytes.size(),0);
        require(empty->atEnd() && empty->pos()==0,"empty range"); rejects([&]{empty->readByte();});
    }
    rejects([&]{buffer.createRange(9,2);}); rejects([&]{buffer.createRange(11,0);});
    rejects([&]{buffer.createRange(std::numeric_limits<size_t>::max(),1);});
    const auto creates=legacy.creates;
    rejects([&]{legacy.createRange(std::numeric_limits<size_t>::max(),1);});
    require(legacy.creates==creates,"overflow reached the factory");
    galuchat::BufferReaderFactory empty(nullptr,0);
    require(empty.createRange(0,0)->atEnd(),"empty memory factory");
}
void checkMapSet(bool layout) {
    Fixture fixture(layout);
    auto factory=std::make_shared<TracedFactory>(fixture.bytes);
    galuchat::GaluchatWGSMapSet3IndexedReader reader(factory);
    require(factory->ranges.empty(),"initialization used range API");
    for (size_t i : {size_t{2},size_t{0},size_t{1}}) {
        const galuchat::Rect target{static_cast<int>(fixture.west[i]),0,1,1};
        factory->ranges.clear();
        const auto expected=reader.readWgsRectWithoutTail(target);
        require(factory->ranges.empty(),"old no-tail API switched to range mode");
        const auto actual=reader.readWgsRectWithoutTailBounded(target);
        require(actual.data==expected.data && factory->ranges.size()==1,"bounded rectangle output/dispatch");
        require(factory->ranges[0].offset==fixture.offsets[i] && factory->ranges[0].length==fixture.images[i].size(),
                "cumulative chunk position or total size is wrong");
    }
    const galuchat::Rect all{0,0,104,64};
    factory->ranges.clear();
    const auto expected=reader.readWgsRect(all);
    require(factory->ranges.empty(),"old full-tail API switched to range mode");
    const auto bounded=reader.readWgsRectWithoutTailBounded(all);
    require(bounded.data==expected.data && factory->ranges.size()==3,"multiple ranges/composition");
    for (size_t i=0;i<3;++i) require(factory->ranges[i].offset==fixture.offsets[i] && factory->ranges[i].length==fixture.images[i].size(),"range order");
    factory->ranges.clear();
    require(reader.readWgsRectWithoutTailBounded({999,999,2,2}).get(0,0)==0 && factory->ranges.empty(),"outside range performed reads");
    galuchat::RawRaster wrong(1,1);
    rejects([&]{reader.readWgsRectWithoutTailBounded({0,0,2,2},wrong);});
    require(factory->ranges.empty(),"invalid target opened a range");
    int64_t code=0;
    require(reader.readWgsPoint(20,0,code) && code==3 && factory->ranges.empty(),"point path changed");
    auto fallback=std::make_shared<LegacyFactory>(fixture.bytes);
    galuchat::GaluchatWGSMapSet3IndexedReader fallback_reader(fallback);
    require(fallback_reader.readWgsRectWithoutTailBounded(all).data==expected.data,"old factory lacks fallback range support");
    fixture.images[1].back()=255; // Invalid MBUInt at the end of an unused final block.
    fixture.rebuild(layout);
    galuchat::GaluchatWGSMapSet3IndexedReader bad(fixture.bytes);
    require(bad.readWgsRectWithoutTailBounded({40,0,1,1}).get(0,0)==7,"bounded API inspected unused tail");
    rejects([&]{bad.readWgsRect({40,0,1,1});});
}
}
int main() {
    checkFactories(); checkMapSet(false); checkMapSet(true);
    std::cout << "chunk ranges: bounds, fallback factories, cumulative positions, variable headers, LAYO, composition, old API dispatch and unused tails OK\n";
}

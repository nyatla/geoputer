#include <algorithm>
#include <climits>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <galuchat/wgsmap3_reader.hpp>

namespace {
using Bytes = std::vector<uint8_t>;
using Decoder = galuchat::GaluchatImageDataChunk01Reader;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void fails(const std::function<void()>& call) {
    try { call(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("expected invalid block range to fail");
}
const std::vector<Bytes> blocks{
    {0, 0x43, 1, 2, 0x60}, // RAW palette and non-byte-aligned pixel payload.
    {0x10, 5, 0x70, 3, 4, 5, 6}, // RAWS direct values.
    {0x20, 6, 0x23, 0x80, 0x38, 0x40, 0x48, 0x50}, // LZSS five literals: 70,7,8,9,10.
    {0, 0x70, 11, 12, 13, 14}
};
Bytes image(int dimension) {
    Bytes payload{uint8_t(dimension), uint8_t(dimension), 1};
    for (const auto& b : blocks) payload.insert(payload.end(), b.begin(), b.end());
    Bytes out{'G','I','0','1',uint8_t(payload.size())};
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
Bytes span(size_t first, size_t count) {
    Bytes out;
    for (size_t i = first; i < first + count; ++i) out.insert(out.end(), blocks[i].begin(), blocks[i].end());
    return out;
}
void rectangles() {
    for (int dim : {3, 4}) {
        const auto bytes = image(dim);
        for (int x = -2; x <= 5; ++x) for (int y = -2; y <= 5; ++y)
        for (int w = 0; w <= 5; ++w) for (int h = 0; h <= 5; ++h) {
            galuchat::BufferReader source(bytes);
            Decoder decoder(source);
            galuchat::RawRaster expected(w, h);
            std::fill(expected.data.begin(), expected.data.end(), -1);
            decoder.readRect(x, y, expected);
            // Includes ranges crossing a block row, and isolated compression modes.
            for (const auto& partition : {std::vector<size_t>{1,1,1,1}, {2,2}, {1,2,1}, {4}}) {
                galuchat::RawRaster actual(w, h);
                std::fill(actual.data.begin(), actual.data.end(), -1);
                size_t first = 0;
                for (auto count : partition) {
                    const auto group = span(first, count);
                    galuchat::BufferReader stream(group);
                    Decoder::readRectFromBlockRange(stream, {dim,dim,2,first,count,group.size()}, x, y, actual);
                    check(stream.pos() <= group.size() && !stream.bitOffset(), "group must stay bounded and byte aligned");
                    first += count;
                }
                check(actual.data == expected.data, "indexed ranges differ from sequential decoder");
            }
        }
    }
    // Invalid data before/after the independently addressed block is never read.
    const Bytes bytes{0x30, 0, 0x70, 11,12,13,14, 0x30};
    galuchat::RawRaster result(1,1);
    const Bytes last = blocks.back();
    galuchat::BufferReader independent(bytes.data()+1, last.size());
    Decoder::readRectFromBlockRange(independent, {4,4,2,3,1,last.size()}, 2,2,result);
    check(result.get(0,0)==11 && independent.atEnd(), "global block numbering must survive restart");
    // Only a single block's bytes are decoded, even if the supplied range has a tail.
    const Bytes prefix = span(0,4);
    galuchat::BufferReader partial(prefix);
    Decoder::readRectFromBlockRange(partial, {4,4,2,0,4,prefix.size()}, 0,0,result);
    check(partial.pos()==blocks[0].size(), "range API consumed an unnecessary tail");
}
void validation() {
    const auto bytes = span(0,4);
    galuchat::RawRaster pixel(1,1);
    const Decoder::BlockRange good{4,4,2,0,4,bytes.size()};
    auto bad = [&](Decoder::BlockRange r) {
        galuchat::BufferReader source(bytes);
        fails([&] { Decoder::readRectFromBlockRange(source,r,0,0,pixel); });
        check(source.pos()==0, "bad metadata must fail before reading bytes");
    };
    auto r=good; r.width=0; bad(r); r=good; r.height=-1; bad(r);
    r=good; r.square_unit=3; bad(r); r=good; r.square_unit=INT_MAX; bad(r);
    r=good; r.first_block=4; bad(r); r=good; r.block_count=SIZE_MAX; bad(r);
    r=good; r.block_count=0; bad(r); r=good; r.data_bytes=0; bad(r);
    r=good; r.width=INT_MAX; r.height=INT_MAX; bad(r);
    galuchat::BufferReader bits(bytes); bits.readBitsAsInt32(1);
    fails([&] { Decoder::readRectFromBlockRange(bits,good,0,0,pixel); });
    // RAWS/LZSS validation and range length limits remain active on restart.
    for (size_t n : {size_t(1),size_t(2)}) {
        auto b=blocks[n]; b.pop_back(); galuchat::BufferReader source(b);
        fails([&] { Decoder::readRectFromBlockRange(source,{4,4,2,n,1,b.size()},int(n%2)*2,int(n/2)*2,pixel); });
    }
    auto b=blocks[3]; b.push_back(0); galuchat::BufferReader extra(b);
    fails([&] { Decoder::readRectFromBlockRange(extra,{4,4,2,3,1,b.size()},2,2,pixel); });
    galuchat::BufferReader short_limit(blocks[3]);
    fails([&] { Decoder::readRectFromBlockRange(short_limit,{4,4,2,3,1,blocks[3].size()-1},2,2,pixel); });
    for (int coordinate : {INT_MIN, INT_MAX}) {
        galuchat::BufferReader outside(bytes);
        Decoder::readRectFromBlockRange(outside,good,coordinate,coordinate,pixel);
        check(!outside.pos(), "outside target must not read blocks");
    }
}
}
int main() {
    rectangles(); validation();
    std::cout << "block ranges: sequential equivalence, RAW/RAWS/LZSS, padding, bounds and early stop OK\n";
}

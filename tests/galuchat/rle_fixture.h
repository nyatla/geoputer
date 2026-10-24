#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace rle_fixture {
using Bytes = std::vector<uint8_t>;
inline void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
inline void append(Bytes& out, const Bytes& data) { out.insert(out.end(), data.begin(), data.end()); }
inline Bytes mb(uint64_t n) {
    if (n < 252) return {uint8_t(n)};
    const uint64_t bases[]{252, 508, 66044, 16843260};
    int bytes = 1;
    while (bytes < 4 && n >= bases[bytes]) ++bytes;
    n -= bases[bytes - 1];
    Bytes out{uint8_t(256 - bytes)};
    for (int i = bytes - 1; i >= 0; --i) out.push_back(uint8_t(n >> (8 * i)));
    return out;
}
struct Bits {
    Bytes data;
    size_t count = 0;
    void put(uint32_t value, int width) {
        for (int i = width - 1; i >= 0; --i) {
            if (!(count % 8)) data.push_back(0);
            data.back() |= ((value >> i) & 1u) << (7 - count % 8);
            ++count;
        }
    }
    void bytes(const Bytes& values) { for (auto b : values) put(b, 8); }
};
inline std::vector<int> runs(int resolution, int pattern) {
    std::vector<int> out;
    int remaining = resolution * resolution;
    while (remaining) {
        int count = pattern == 0 ? remaining : pattern == 1 ? 1
            : std::min(remaining, int((out.size() * 29 + 3) % (resolution * 3 + 1)) + 1);
        out.push_back(count); remaining -= count;
    }
    return out;
}
inline int paletteIndex(int run, int palette) { return palette == 3 ? (run * 7) % 16 : run % (palette == 0 ? 2 : palette == 1 ? 3 : 5); }
inline Bits encodedCell(int mode, int palette, const std::vector<int>& lengths,
                  uint64_t first_value = 0) {
    const int sizes[]{2,3,5,16}, widths[]{7,9,12,20};
    Bits bits;
    bits.put(0x80 | (palette << 4) | mode, 8);
    bits.put(((1u << sizes[palette]) - 1) << 4, widths[palette]);
    for (int i = 0; i < sizes[palette]; ++i) bits.bytes(mb(i == 0 ? first_value : uint64_t(i * 11)));
    for (size_t i = 0; i < lengths.size(); ++i) {
        bits.bytes(mb(lengths[i]));
        if (palette == 3) bits.put(paletteIndex(int(i), palette), 4);
        else if (palette && i) bits.put(0, palette == 1 ? 1 : 2);
    }
    return bits;
}
inline Bytes cell(int mode, int palette, const std::vector<int>& lengths, uint64_t first_value = 0) {
    return encodedCell(mode,palette,lengths,first_value).data;
}
inline Bytes block(int compression, const Bytes& decoded) {
    Bytes out{uint8_t(compression << 4)};
    if (!compression) append(out, decoded);
    else {
        Bytes payload = decoded;
        if (compression == 2) {
            Bits bits;
            for (size_t i = 0; i < decoded.size(); i += 16) {
                size_t size = std::min<size_t>(16, decoded.size() - i);
                bits.put(0, 1); bits.put(size - 1, 4);
                for (size_t j = 0; j < size; ++j) bits.put(decoded[i + j], 8);
            }
            payload = bits.data;
        }
        append(out, mb(payload.size())); append(out, payload);
    }
    return out;
}
inline Bytes chunk(const char* tag, const Bytes& payload) {
    Bytes out(tag, tag + 4); append(out, mb(payload.size())); append(out, payload); return out;
}
inline Bytes image(int resolution, const std::vector<Bytes>& blocks, int columns = 1) {
    Bytes payload; append(payload, mb(resolution * columns));
    append(payload, mb(resolution * int(blocks.size() / columns)));
    int exponent = 0; while ((1 << exponent) < resolution) ++exponent;
    payload.push_back(exponent);
    for (const auto& b : blocks) append(payload, b);
    return chunk("GI01", payload);
}
// Independent geometric oracle: build the ordered serpentine traversal explicitly.
inline std::vector<int64_t> expected(int resolution, int mode, int palette,
                                     const std::vector<int>& lengths) {
    std::vector<int64_t> stream;
    for (size_t i = 0; i < lengths.size(); ++i)
        stream.insert(stream.end(), lengths[i], paletteIndex(int(i), palette) * 11);
    std::vector<int64_t> pixels(stream.size());
    size_t index = 0;
    for (int step = 0; step < resolution; ++step) {
        const int outer = mode == 3 ? resolution - 1 - step : step;
        bool forward = outer % 2 == 0;
        if (mode == 1) forward = !forward;
        for (int p = 0; p < resolution; ++p) {
            const int inner = forward ? p : resolution - 1 - p;
            const int x = mode < 2 ? inner : outer, y = mode < 2 ? outer : inner;
            pixels[size_t(y) * resolution + x] = stream[index++];
        }
    }
    return pixels;
}
} // namespace rle_fixture

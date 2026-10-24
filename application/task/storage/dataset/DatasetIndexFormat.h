#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace geoputer::index_format {
// _mapindex.dat version 1 wire layout. Never serialize C++ struct padding.
inline uint32_t get(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline void put(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}
inline void put16(uint8_t* p, uint16_t value) { p[0] = static_cast<uint8_t>(value); p[1] = static_cast<uint8_t>(value >> 8); }

template<size_t N, class Reader> std::array<uint8_t, N> readRecord(Reader& source) {
    std::array<uint8_t, N> bytes{};
    for (auto& byte : bytes) byte = source.readByte();
    return bytes;
}

namespace Header {
inline constexpr size_t Bytes = 64;
inline constexpr size_t Version = 8;
inline constexpr size_t HeaderBytes = 10;
inline constexpr size_t ChunkRecordBytes = 12;
inline constexpr size_t GroupRecordBytes = 14;
inline constexpr size_t FileBytes = 16;
inline constexpr size_t MapBytes = 20;
inline constexpr size_t MapFatMtime = 24;
inline constexpr size_t MapNameCrc32 = 28;
inline constexpr size_t MapHeaderCrc32 = 32;
inline constexpr size_t ChunkCount = 36;
inline constexpr size_t UnitInvX = 40;
inline constexpr size_t UnitInvY = 44;
inline constexpr size_t IndexedChunkMinBytes = 48;
inline constexpr size_t GroupTargetBytes = 52;
inline constexpr size_t WordbookCount = 56;
inline constexpr size_t MapIndexBytes = 60;
}

namespace ChunkRecord {
inline constexpr size_t Bytes = 40;
inline constexpr size_t West = 0;
inline constexpr size_t South = 4;
inline constexpr size_t Width = 8;
inline constexpr size_t Height = 12;
inline constexpr size_t SquareUnit = 16;
inline constexpr size_t ChunkOffset = 20;
inline constexpr size_t ChunkBytes = 24;
inline constexpr size_t FirstBlockOffset = 28;
inline constexpr size_t BlockCount = 32;
inline constexpr size_t GroupCount = 36;
}

namespace Group {
inline constexpr size_t Bytes = 16;
inline constexpr size_t FirstBlock = 0;
inline constexpr size_t BlockCount = 4;
inline constexpr size_t DataOffset = 8;
inline constexpr size_t DataBytes = 12;
}

namespace Wordbook {
inline constexpr size_t Bytes = 128;
inline constexpr size_t Language = 0;
inline constexpr size_t NameCrc32 = 32;
inline constexpr size_t SourceBytes = 36;
inline constexpr size_t FatMtime = 40;
inline constexpr size_t HeaderCrc32 = 44;
inline constexpr size_t SectionBytes = 48;
inline constexpr size_t TiDataOffset = 52;
inline constexpr size_t TiDataBytes = 56;
inline constexpr size_t TtDataOffset = 60;
inline constexpr size_t TtDataBytes = 64;
inline constexpr size_t TmDataOffset = 68;
inline constexpr size_t TmDataBytes = 72;
inline constexpr size_t RecordCount = 76;
inline constexpr size_t ComponentCount = 80;
inline constexpr size_t TokenCount = 84;
inline constexpr size_t Depth = 88;
inline constexpr size_t CodeBits = 90;
inline constexpr size_t TokenBits = 91;
inline constexpr size_t RootCount = 92;
inline constexpr size_t TextPageCount = 96;
inline constexpr size_t TokenPageCount = 100;
inline constexpr size_t TiRecordBytes = 104;
inline constexpr size_t TtRecordBytes = 106;
inline constexpr size_t TmRecordBytes = 108;
inline constexpr size_t HeaderBytes = 110;
inline constexpr size_t Reserved = 112;
inline constexpr size_t LanguageBytes = 32;
}

namespace TiRoot {
inline constexpr size_t Bytes = 16;
inline constexpr size_t FirstRecord = 0;
inline constexpr size_t RecordCount = 4;
inline constexpr size_t DataOffset = 8;
inline constexpr size_t StartBit = 12;
}

namespace TtPage {
inline constexpr size_t Bytes = 8;
inline constexpr size_t FirstCode = 0;
inline constexpr size_t DataOffset = 4;
}

namespace TmPage {
inline constexpr size_t Bytes = 16;
inline constexpr size_t FirstToken = 0;
inline constexpr size_t TokenCount = 4;
inline constexpr size_t TokenBytes = 8;
inline constexpr size_t DataOffset = 12;
}

} // namespace geoputer::index_format

#pragma once

#include <cstddef>
#include <cstdint>

namespace geoputer::config {

// Cardputer ADV EXT pins reserved for disabling the unused Mesh Kit radio.
inline constexpr uint8_t LoraResetPin = 3;
inline constexpr uint8_t LoraSelectPin = 5;
inline constexpr uint8_t LoraBusyPin = 6;
inline constexpr uint32_t LoraBusyTimeoutMs = 100;
inline constexpr uint32_t LoraSpiHz = 1000000;

inline constexpr uint8_t UiColorDepth = 4;       // 16-color palette; 16 selects the RGB565 reference path.
inline constexpr uint8_t SdMaxOpenFiles = 3;     // GIS initialization needs two simultaneous VFS readers.

// Galuchat resource limits and access tuning. These are firmware settings;
// user-editable settings and their defaults remain in Settings.h / Settings.cpp.
inline constexpr size_t MaxDatasetChunks = 1500;  // Inclusive; never fall back to sequential scanning.
inline constexpr int MapCacheSide = 128;          // uint32 pixels; 128 * 128 * 4 = 64 KiB.
inline constexpr size_t FileReadBufferBytes = 4096;
inline constexpr size_t FileReadThroughSkipBytes = FileReadBufferBytes; // Inclusive gap beyond buffered data; 0 disables.
inline constexpr size_t WordBookCacheEntries = 16;
inline constexpr uint32_t PlaceSearchIntervalMs = 5000;
inline constexpr size_t DatasetLineBytes = 256;  // 255 UTF-8 bytes plus NUL.
inline constexpr size_t DatasetLanguageBytes = 32;
inline constexpr unsigned DatasetPageRows = 4;
inline constexpr unsigned PlaceLinePixels = 232; // 240px display, 4px margins.
inline constexpr unsigned ExportRecordsPerStep = 10;
inline constexpr size_t ExportWriteBufferBytes = 2048;
inline constexpr uint32_t ExportProgressIntervalMs = 200;

static_assert(MaxDatasetChunks > 0, "A dataset must allow at least one chunk");
static_assert(UiColorDepth == 4 || UiColorDepth == 16, "Supported UI frame formats");
static_assert(SdMaxOpenFiles >= 2, "Map header validation opens two readers");
static_assert(MapCacheSide > 0 && FileReadBufferBytes > 0, "Read buffers must be nonempty");
static_assert(WordBookCacheEntries > 0 && WordBookCacheEntries <= UINT8_MAX,
              "WordBook cache indices are eight bits");
static_assert(ExportRecordsPerStep > 0 && ExportRecordsPerStep <= 16, "Export read buffer holds sixteen records");
static_assert(ExportWriteBufferBytes > 0, "Export write buffer must not be empty");

}  // namespace geoputer::config

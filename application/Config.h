#pragma once

#include <cstddef>
#include <cstdint>

// Enable only for diagnostic firmware / profiling tests.
#ifndef REVERSEGEOPUTER_DIAGNOSTICS
#define REVERSEGEOPUTER_DIAGNOSTICS 0
#endif

namespace geoputer::config {
inline constexpr bool DetailedDiagnostics = REVERSEGEOPUTER_DIAGNOSTICS != 0;


// Cardputer ADV EXT pins reserved for disabling the unused Mesh Kit radio.
inline constexpr uint8_t LoraResetPin = 3;
inline constexpr uint8_t LoraSelectPin = 5;
inline constexpr uint8_t LoraBusyPin = 6;
inline constexpr uint32_t LoraBusyTimeoutMs = 100;
inline constexpr uint32_t LoraSpiHz = 1000000;

inline constexpr uint32_t BootMaintenanceWindowMs = 1500; // After settings load; held ESC bypasses this wait.

inline constexpr uint8_t UiColorDepth = 4;       // 16-color palette; 16 selects the RGB565 reference path.
inline constexpr uint32_t SdSpiHz = 25'000'000; // Requested SD SPI limit; current 80MHz APB divider selects 20MHz. Shared by all SD paths.
inline constexpr uint8_t SdMaxOpenFiles = 3;     // GIS initialization needs two simultaneous VFS readers.

// Galuchat resource limits and access tuning. These are firmware settings;
// user-editable settings and their defaults remain in Settings.h / Settings.cpp.
inline constexpr size_t MaxDatasetChunks = 1500;  // Inclusive; never fall back to sequential scanning.
inline constexpr int MapCacheSide = 128;          // uint32 pixels; 128 * 128 * 4 = 64 KiB.
inline constexpr size_t FileReadBufferBytes = 4096; // Generic Reader default.
inline constexpr size_t MapInitReadBufferBytes = 4096;
inline constexpr size_t MapRectReadBufferBytes = 16384; // Rectangle fills only; measured against 4/8KiB.
inline constexpr size_t WordBookReadBufferBytes = 4096;
inline constexpr size_t FileReadThroughSkipBytes = 4096; // Inclusive gap beyond buffered data; independent of buffer size; 0 disables.
// Dataset index generation / streaming access format v1.
inline constexpr size_t IndexReadBufferBytes = 4096;
inline constexpr uint32_t IndexMinChunkBytes = 16384;
inline constexpr uint32_t IndexGroupTargetBytes = 65536;
inline constexpr uint16_t IndexMaxDepth = 32; // Bounds recursive TI00 parsing during generation.
inline constexpr size_t WordBookCacheEntries = 16;
// Place lookup follows each changed dataset cell; no periodic throttle.
// Independent boundary-map buffers; the 128x128 point cache is untouched.
inline constexpr size_t MapViewCodeMaxBytes = 96 * 1024;
inline constexpr unsigned MapViewRowsPerStep = 8;
inline constexpr uint8_t MapViewScale = 1; // Boot scale; Map Up/Down selects 1, 2, 4 for this session.
inline constexpr unsigned MapCursorRadius = 32; // Display pixels in each direction from the committed map center.
inline constexpr uint32_t MapViewRetryMs = 1000;
inline constexpr bool MapHeapDiagnostics = DetailedDiagnostics; // Phase snapshots; no extra map buffers or sampling task.
inline constexpr size_t DatasetLineBytes = 256;  // 255 UTF-8 bytes plus NUL.
inline constexpr size_t DatasetLanguageBytes = 32;
inline constexpr unsigned DatasetPageRows = 4;
inline constexpr unsigned PlaceLinePixels = 232; // 240px display, 4px margins.
inline constexpr unsigned ExportRecordsPerStep = 10;
inline constexpr size_t ExportWriteBufferBytes = 2048;
inline constexpr uint32_t ExportProgressIntervalMs = 200;

static_assert(IndexReadBufferBytes > 0 && IndexMinChunkBytes > 0 && IndexGroupTargetBytes > 0 && IndexMaxDepth > 0,
              "Index resource limits must be nonzero");
static_assert(MapViewScale == 1 || MapViewScale == 2 || MapViewScale == 4, "Supported map decimation");
static_assert(MaxDatasetChunks > 0, "A dataset must allow at least one chunk");
static_assert(UiColorDepth == 4 || UiColorDepth == 16, "Supported UI frame formats");
static_assert(SdMaxOpenFiles >= 2, "Map header validation opens two readers");
static_assert(MapCacheSide > 0 && FileReadBufferBytes > 0 && MapInitReadBufferBytes > 0 &&
              MapRectReadBufferBytes > 0 && WordBookReadBufferBytes > 0, "Read buffers must be nonempty");
static_assert(WordBookCacheEntries > 0 && WordBookCacheEntries <= UINT8_MAX,
              "WordBook cache indices are eight bits");
static_assert(ExportRecordsPerStep > 0 && ExportRecordsPerStep <= 16, "Export read buffer holds sixteen records");
static_assert(ExportWriteBufferBytes > 0, "Export write buffer must not be empty");

}  // namespace geoputer::config

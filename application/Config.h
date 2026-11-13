#pragma once

#include <cstddef>
#include <cstdint>

// Enable only for diagnostic firmware / profiling tests.
#ifndef GEOPUTER_DIAGNOSTICS
#define GEOPUTER_DIAGNOSTICS 0
#endif

namespace geoputer::config {
inline constexpr bool DetailedDiagnostics = GEOPUTER_DIAGNOSTICS != 0;
inline constexpr unsigned LogStatisticsBatchRecords = 8;
inline constexpr unsigned LogStatisticsStepsPerLoop = 32;
inline constexpr uint32_t LogStatisticsBudgetUs = 2000;
inline constexpr uint32_t LogStatisticsProgressMs = 200;
// Per-point display intervals: quarter, half, normal, double and quadruple replay.
inline constexpr uint32_t LogReplayIntervalsMs[] = {4000,2000,1000,500,250};
inline constexpr uint8_t LogReplayDefaultSpeed = 2;


// Cardputer ADV EXT pins reserved for disabling the unused Mesh Kit radio.
inline constexpr uint8_t LoraResetPin = 3;
inline constexpr uint8_t LoraSelectPin = 5;
inline constexpr uint8_t LoraBusyPin = 6;
inline constexpr uint32_t LoraBusyTimeoutMs = 100;
inline constexpr uint32_t LoraSpiHz = 1000000;

inline constexpr uint32_t BootMaintenanceWindowMs = 1500; // After settings load; held ESC bypasses this wait.

inline constexpr uint8_t UiColorDepth = 4;       // 16-color palette; 16 selects the RGB565 reference path.
inline constexpr uint32_t SdSpiHz = 25'000'000; // Requested SD SPI limit; current 80MHz APB divider selects 20MHz. Shared by all SD paths.
inline constexpr uint8_t SdMaxOpenFiles = 3;     // VFS descriptors for metadata/browsing; native FIL loans are separate.

inline constexpr char LogDirectory[] = "/sd/logs"; // Fixed firmware log directory.
inline constexpr size_t TemporaryPathBytes = 256; // Full temporary UTF-8 path, including the terminating NUL.

// Galuchat resource limits and access tuning. These are firmware settings;
// user-editable settings and their defaults remain in Settings.h / Settings.cpp.
inline constexpr size_t MaxDatasetChunks = 1500;  // Inclusive; never fall back to sequential scanning.
inline constexpr int MapCacheSide = 128;          // uint32 pixels; 128 * 128 * 4 = 64 KiB.
inline constexpr size_t FileReadBufferBytes = 4096; // Generic Reader default.
inline constexpr size_t MapInitReadBufferBytes = 4096;
inline constexpr size_t MapRectReadBufferBytes = 4096; // GLC and index sequentially borrow the same UI workspace.
inline constexpr size_t WordBookReadBufferBytes = 4096;
inline constexpr size_t FileReadThroughSkipBytes = 4096; // Inclusive gap beyond buffered data; independent of buffer size; 0 disables.
// Dataset index generation / streaming access format v1.
inline constexpr size_t IndexReadBufferBytes = 4096;
static_assert(MapRectReadBufferBytes <= IndexReadBufferBytes && WordBookReadBufferBytes <= IndexReadBufferBytes,
              "GLC and Wordbook Readers must fit the shared UI read workspace");
inline constexpr uint32_t IndexMinChunkBytes = 16384;
inline constexpr uint32_t IndexGroupTargetBytes = 65536;
// Whole-request stack cache; only requests exceeding 15 references need another scan.
inline constexpr size_t MapReadGroupCacheEntries = 15;
inline constexpr uint16_t IndexMaxDepth = 32; // Bounds recursive TI00 parsing during generation.
// Per-phase stack references; never retained across TT/TM phases or searches.
inline constexpr size_t WordBookReadPageCacheEntries = 32;
inline constexpr size_t WordBookCacheEntries = 16;
inline constexpr size_t WordBookLookupTokens = 128; // One component; uint16 token IDs, no heap fallback.
inline constexpr size_t PlaceAsciiLimit = 128, PlaceWideLimit = 64;
inline constexpr size_t PlaceResultLineBytes = PlaceAsciiLimit + PlaceWideLimit * 4 + 1;
inline constexpr size_t PlaceCachedLineBytes = 96; // Two 8px rows of UTF-8 names, including terminator.
// Place lookup follows each changed dataset cell; no periodic throttle.
// Independent boundary-map buffers; the 128x128 point cache is untouched.
inline constexpr size_t MapViewCodeMaxBytes = 96 * 1024;
inline constexpr unsigned MapViewRowsPerStep = 8;
inline constexpr uint8_t MapViewScale = 1; // Boot scale; Map +/- selects 1, 2, 4 for this session.
inline constexpr unsigned MapCursorRadius = 32; // Display pixels in each direction from the committed map center.
inline constexpr uint32_t MapViewRetryMs = 1000;
inline constexpr bool MapHeapDiagnostics = DetailedDiagnostics; // Phase snapshots; no extra map buffers or sampling task.
inline constexpr char DatasetRoot[] = "/sd/dataset/"; // Firmware root; not stored in the selected session.
inline constexpr size_t DatasetFilenameBytes = 128; // UTF-8 filename including extension and terminating NUL.
inline constexpr size_t DatasetLineBytes = 256;  // 255 UTF-8 bytes plus NUL.
inline constexpr size_t DatasetLanguageBytes = 32;
inline constexpr unsigned MaxDatasetLanguages = 16; // Reject additional INI language sections.
inline constexpr size_t PlaceFormatBytes = 128; // Up to 127 UTF-8 bytes plus NUL, per row.
inline constexpr unsigned DatasetPageRows = 4;
inline constexpr unsigned PlaceLinePixels = 232; // 240px display, 4px margins.
inline constexpr unsigned WifiPageEntries = 20;
inline constexpr size_t WifiTransferBytes = 4096;
inline constexpr uint32_t WifiTextMaxBytes = 64 * 1024;
inline constexpr uint32_t WifiUploadMaxBytes = 0x7fffffff; // WebServer's signed Content-Length.
inline constexpr unsigned WifiDeleteMaxDepth = 16;
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
static_assert(WordBookReadPageCacheEntries > 0 && WordBookReadPageCacheEntries <= 32,
              "Wordbook phase references must fit the reviewed stack budget");
static_assert(WordBookCacheEntries > 0 && WordBookCacheEntries <= UINT8_MAX,
              "WordBook cache indices are eight bits");
static_assert(ExportRecordsPerStep > 0 && ExportRecordsPerStep <= 16, "Export read buffer holds sixteen records");
static_assert(ExportWriteBufferBytes > 0, "Export write buffer must not be empty");

}  // namespace geoputer::config

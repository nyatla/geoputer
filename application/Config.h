#pragma once

#include <cstddef>
#include <cstdint>

namespace geoputer::config {

// Galuchat resource limits and access tuning. These are firmware settings;
// user-editable settings and their defaults remain in Settings.h / Settings.cpp.
inline constexpr size_t MaxDatasetChunks = 1500;  // Inclusive; never fall back to sequential scanning.
inline constexpr int MapCacheSide = 128;          // uint32 pixels; 128 * 128 * 4 = 64 KiB.
inline constexpr size_t FileReadBufferBytes = 4096;
inline constexpr size_t WordBookCacheEntries = 16;
inline constexpr uint32_t PlaceSearchIntervalMs = 5000;

static_assert(MaxDatasetChunks > 0, "A dataset must allow at least one chunk");
static_assert(MapCacheSide > 0 && FileReadBufferBytes > 0, "Read buffers must be nonempty");
static_assert(WordBookCacheEntries > 0 && WordBookCacheEntries <= UINT8_MAX,
              "WordBook cache indices are eight bits");

}  // namespace geoputer::config

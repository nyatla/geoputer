#pragma once
#include <cstddef>
#include <cstdint>

namespace geoputer::map {
using RegionCode = uint32_t;
enum class Error : uint8_t { None, InvalidInput, TooLarge, NoMemory, NoFix, NoDataset, NoCard, ReadError, Stale };
struct Request {
    uint32_t token = 0, screen_generation = 0, dataset_generation = 0, sample_seq = 0;
    double latitude = 0, longitude = 0;
    uint16_t row = 0;
};
struct FrameInfo {
    Request request;
    uint16_t width = 0, height = 0;
    double meters_per_pixel = 0;
    RegionCode region = 0;
    Error error = Error::None;
    char name[64]{};
};
constexpr size_t TransferRows = 8, MaxWidth = 240, MaxHeight = 101;
constexpr size_t TransferBytes = ((MaxWidth + 3) / 4) * TransferRows;
struct Transfer {
    FrameInfo info;
    uint16_t row = 0, rows = 0;
    uint8_t pixels[TransferBytes]{};
};
struct ViewState { bool busy = false; Error error = Error::None; };
struct GridRect { int32_t x = 0, y = 0, width = 0, height = 0; };
struct RasterView {
    const RegionCode* pixels = nullptr;
    GridRect bounds;
    size_t count = 0;
    RegionCode get(int x, int y) const { return pixels[size_t(y - bounds.y) * bounds.width + (x - bounds.x)]; }
};
inline const char* errorText(Error error) {
    switch (error) {
        case Error::NoFix: return "WAIT GPS FIX";
        case Error::NoDataset: return "NO DATASET";
        case Error::NoCard: return "SD NOT READY";
        case Error::NoMemory: return "MAP NO MEMORY";
        case Error::TooLarge: return "MAP TOO LARGE";
        case Error::ReadError: return "MAP READ ERROR";
        case Error::Stale: return "MAP CHANGED";
        case Error::InvalidInput: return "MAP INVALID";
        default: return "";
    }
}
} // namespace geoputer::map

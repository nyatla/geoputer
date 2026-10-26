#pragma once
#include <cstddef>
#include <cstdint>

namespace geoputer::map {
using RegionCode = uint32_t;
enum class SessionId : uint8_t { Live, Log };
enum class Error : uint8_t { None, InvalidInput, TooLarge, NoMemory, NoFix, NoDataset, NoCard, ReadError, Stale };
constexpr size_t MaxWidth = 240, MaxHeight = 101;
constexpr size_t BoundaryBytes = ((MaxWidth + 3) / 4) * MaxHeight;
// A UI-owned back image, lent to one Storage operation until explicit return.
struct BufferLease {
    uint8_t* data = nullptr;
    size_t capacity = 0;
    uint32_t id = 0;
};
inline bool validBuffer(const BufferLease& buffer) {
    return buffer.data && buffer.id && buffer.capacity >= BoundaryBytes;
}
inline bool sameBuffer(const BufferLease& a,const BufferLease& b) {
    return a.data==b.data && a.capacity==b.capacity && a.id==b.id;
}
struct Request {
    uint32_t token = 0, screen_generation = 0, dataset_generation = 0, sample_seq = 0;
    double latitude = 0, longitude = 0;
    BufferLease buffer;
    uint8_t scale = 1; // Input cells per stored sample: 1, 2 or 4.
    SessionId session = SessionId::Live;
    RegionCode region = 0;
    bool region_known = false, reuse = false;
};
struct FrameInfo {
    Request request;
    uint16_t width = 0, height = 0;
    double meters_per_pixel = 0;
    RegionCode region = 0; // Highlighted region; may be updated from retained pixels without moving the map.
    Error error = Error::None;
};
inline bool sameRequest(const Request& a,const Request& b) {
    return a.token==b.token && a.screen_generation==b.screen_generation &&
        a.dataset_generation==b.dataset_generation && a.session==b.session &&
        a.scale==b.scale && sameBuffer(a.buffer,b.buffer);
}
constexpr bool supportedScale(unsigned scale) { return scale==1 || scale==2 || scale==4; }
inline const char* scaleText(unsigned scale) { return scale==4?"1/4":scale==2?"1/2":"1/1"; }
struct ViewState { bool busy = false; Error error = Error::None; uint8_t scale = 1; };
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

#pragma once

#include <cstddef>
#include <cstdint>
#include "../../Config.h"

namespace geoputer {
// Task-owned backing storage, never a local on the UI task stack.
constexpr size_t uiFrameBufferBytes(uint8_t depth) {
    return 240 * 135 * depth / 8 + (depth == 16 ? 2 : 1);
}
struct UiFrameBuffer {
    alignas(4) uint8_t data[(uiFrameBufferBytes(config::UiColorDepth) + 3) & ~size_t(3)]{};
};
} // namespace geoputer

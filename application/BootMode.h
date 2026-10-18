#pragma once
#include <cstdint>

namespace geoputer {
enum class BootMode : uint8_t { GpsLogger, UsbStorage };
// Separate from the versioned user settings; read before starting any task.
bool loadBootMode(BootMode& mode);
bool saveBootMode(BootMode mode);
}

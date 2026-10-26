#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace geoputer {
inline std::string fatFsPath(uint8_t drive, const std::string& path) {
    if (path.compare(0, 4, "/sd/") != 0) throw std::runtime_error("SD path is outside /sd");
    auto native = std::to_string(drive);
    native.reserve(native.size() + 1 + path.size() - 3);
    native.push_back(':');
    native.append(path, 3, std::string::npos);
    return native;
}

// Native paths remain unchanged in the host filesystem adapters.
inline std::string storageNativePath(uint8_t drive, const std::string& path) {
#ifdef ARDUINO_ARCH_ESP32
    return fatFsPath(drive, path);
#else
    (void)drive;
    return path;
#endif
}
} // namespace geoputer

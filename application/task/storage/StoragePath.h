#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>

namespace geoputer {
// Optional non-owning scratch. An explicitly supplied but invalid/short view
// must fail instead of silently allocating a replacement.
struct NativePathBuffer {
    char* data=nullptr;
    size_t capacity=0;
    bool supplied()const{return data || capacity;}
};
inline const char* fatFsPath(uint8_t drive,const std::string& path,NativePathBuffer target) {
    if(path.compare(0,4,"/sd/")!=0)throw std::runtime_error("SD path is outside /sd");
    const size_t digits=drive>=100?3:drive>=10?2:1;
    const size_t suffix=path.size()-3;
    if(!target.data || target.capacity<digits+2 || suffix>target.capacity-digits-2)
        throw std::length_error("FatFs path exceeds borrowed capacity");
    unsigned value=drive;
    for(size_t n=digits;n;value/=10)target.data[--n]=static_cast<char>('0'+value%10);
    target.data[digits]=':';
    std::memcpy(target.data+digits+1,path.data()+3,suffix);
    target.data[digits+1+suffix]='\0';
    return target.data;
}

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

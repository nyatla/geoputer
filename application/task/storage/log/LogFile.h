#pragma once
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <string>
#ifdef ARDUINO_ARCH_ESP32
#include <ff.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace geoputer {
// FatFs uses a 32-bit unsigned offset, including the upper half of FAT32 files.
// Host tests use the same operations through stdio with a 64-bit host offset.
class LogFile {
public:
    void setDrive(uint8_t drive) { drive_ = drive; }
    bool opened() const { return opened_; }
    bool open(const std::string& path, bool exclusive) {
#ifdef ARDUINO_ARCH_ESP32
        const auto native = std::to_string(drive_) + ":" + path.substr(3);
        const auto result = f_open(&file_, native.c_str(), FA_READ | FA_WRITE | (exclusive ? FA_CREATE_NEW : FA_OPEN_EXISTING));
        if (result != FR_OK) { errno = result == FR_EXIST ? EEXIST : EIO; return false; }
#else
        const int fd = ::open(path.c_str(), O_RDWR | (exclusive ? O_CREAT | O_EXCL : 0), 0664);
        if (fd < 0) return false;
        file_ = fdopen(fd, "r+b");
        if (!file_) { ::close(fd); return false; }
#endif
        opened_ = true; return true;
    }
    bool seek(uint64_t offset) {
#ifdef ARDUINO_ARCH_ESP32
        return offset <= UINT32_MAX && f_lseek(&file_, static_cast<FSIZE_t>(offset)) == FR_OK && f_tell(&file_) == offset;
#else
        std::clearerr(file_);
        return fseeko(file_, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
    }
    bool openRead(const std::string& path) {
#ifdef ARDUINO_ARCH_ESP32
        const auto native=std::to_string(drive_)+":"+path.substr(3);
        if (f_open(&file_,native.c_str(),FA_READ)!=FR_OK) return false;
#else
        file_=std::fopen(path.c_str(),"rb"); if (!file_) return false;
#endif
        opened_=true; return true;
    }
    bool read(uint8_t* bytes,size_t count) {
#ifdef ARDUINO_ARCH_ESP32
        UINT received=0;
        return f_read(&file_,bytes,count,&received)==FR_OK && received==count;
#else
        return std::fread(bytes,1,count,file_)==count;
#endif
    }
    bool size(uint64_t& bytes) {
#ifdef ARDUINO_ARCH_ESP32
        bytes=f_size(&file_); return true;
#else
        struct stat info{};
        if (fstat(fileno(file_),&info) || info.st_size<0) return false;
        bytes=static_cast<uint64_t>(info.st_size); return true;
#endif
    }
    bool write(const uint8_t* bytes, size_t count) {
#ifdef ARDUINO_ARCH_ESP32
        UINT written = 0;
        return f_write(&file_, bytes, count, &written) == FR_OK && written == count;
#else
        return std::fwrite(bytes, 1, count, file_) == count;
#endif
    }
    bool sync() {
#ifdef ARDUINO_ARCH_ESP32
        return f_sync(&file_) == FR_OK;
#else
        return std::fflush(file_) == 0 && ::fsync(fileno(file_)) == 0;
#endif
    }
    bool close() {
        if (!opened_) return true;
#ifdef ARDUINO_ARCH_ESP32
        // FatFs keeps the object valid after a failed f_close; retain it to retry.
        if (f_close(&file_) != FR_OK) return false;
#else
        const int result = std::fclose(file_); file_ = nullptr;
        opened_ = false;
        return result == 0;
#endif
        opened_ = false; return true;
    }
private:
    uint8_t drive_ = 0;
    bool opened_ = false;
#ifdef ARDUINO_ARCH_ESP32
    FIL file_{};
#else
    std::FILE* file_ = nullptr;
#endif
};
}  // namespace geoputer

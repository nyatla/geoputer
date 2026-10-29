#pragma once
#include <cassert>
#include <array>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include "StoragePath.h"
#ifdef ARDUINO_ARCH_ESP32
#include "StorageFs.h"
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace geoputer {
class ReadFile;
// A Storage-owned native file slot. Its owner outlives every short-lived loan.
// Persistent recording/export files have independent owners.
class ReadFileSlot {
public:
    ReadFileSlot()=default;
    ~ReadFileSlot() {assert(!used_);}
    ReadFileSlot(const ReadFileSlot&)=delete;
    ReadFileSlot& operator=(const ReadFileSlot&)=delete;
    bool inUse()const{return used_;}
private:
    friend class ReadFile;
#ifdef ARDUINO_ARCH_ESP32
    FIL file_{};
#endif
    bool used_=false;
};
// Stable across map/log/index job changes; no third-handle fallback.
class ReadFileSlots {
public:
    ReadFileSlot& primary() {return slots_[0];}
    ReadFileSlot& secondary() {return slots_[1];}
    ReadFileSlot* available() {
        for(auto& slot:slots_)if(!slot.inUse())return &slot;
        throw std::logic_error("Storage file slots exhausted");
    }
    bool inUse()const {return slots_[0].inUse() || slots_[1].inUse();}
private:
    std::array<ReadFileSlot,2> slots_;
};
// Short-lived reads and transactional staging output can borrow a native FIL.
// Persistent log writes retain their separate LogFile close/retry policy.
class ReadFile {
public:
    explicit ReadFile(uint8_t drive=0,ReadFileSlot* slot=nullptr):slot_(slot),drive_(drive){}
    ~ReadFile() {close();}
    ReadFile(const ReadFile&)=delete;
    ReadFile& operator=(const ReadFile&)=delete;
    bool opened()const{return opened_;}
    void setDrive(uint8_t drive) {assert(!opened_);drive_=drive;}
    bool openRead(FileReadPath path) {
        acquire();
        try {
#ifdef ARDUINO_ARCH_ESP32
            file_=&slot_->file_;
            opened_=f_open(file_,fatFsPath(drive_,path).c_str(),FA_READ)==FR_OK;
#else
            file_=std::fopen(path.resolve().c_str(),"rb");opened_=file_!=nullptr;
#endif
            if(!opened_)release();
            return opened_;
        } catch(...) {release();throw;}
    }
    bool openStaging(FileReadPath path) {
        acquire();
        try {
#ifdef ARDUINO_ARCH_ESP32
            file_=&slot_->file_;
            opened_=f_open(file_,fatFsPath(drive_,path).c_str(),FA_READ|FA_WRITE|FA_CREATE_NEW)==FR_OK;
#else
            const int fd=::open(path.resolve().c_str(),O_RDWR|O_CREAT|O_EXCL,0664);
            if(fd>=0) {file_=fdopen(fd,"r+b");if(!file_)::close(fd);}
            opened_=file_!=nullptr;
#endif
            if(!opened_)release();
            return opened_;
        } catch(...) {release();throw;}
    }
    bool seek(uint64_t offset) {
#ifdef ARDUINO_ARCH_ESP32
        return offset<=std::numeric_limits<FSIZE_t>::max()
            && f_lseek(file_,static_cast<FSIZE_t>(offset))==FR_OK && f_tell(file_)==offset;
#else
        return offset<=static_cast<uint64_t>(std::numeric_limits<long>::max())
            && std::fseek(file_,static_cast<long>(offset),SEEK_SET)==0;
#endif
    }
    bool read(uint8_t* target,size_t count,size_t& received) {
#ifdef ARDUINO_ARCH_ESP32
        UINT n=0;const auto result=f_read(file_,target,static_cast<UINT>(count),&n);received=n;
        return result==FR_OK;
#else
        received=std::fread(target,1,count,file_);return !std::ferror(file_);
#endif
    }
    bool read(uint8_t* target,size_t count) {size_t received=0;return read(target,count,received) && received==count;}
    bool write(const uint8_t* data,size_t count) {
#ifdef ARDUINO_ARCH_ESP32
        UINT written=0;return f_write(file_,data,count,&written)==FR_OK && written==count;
#else
        return std::fwrite(data,1,count,file_)==count;
#endif
    }
    bool sync() {
#ifdef ARDUINO_ARCH_ESP32
        return f_sync(file_)==FR_OK;
#else
        return std::fflush(file_)==0 && ::fsync(fileno(file_))==0;
#endif
    }
    bool size(uint64_t& bytes)const {
#ifdef ARDUINO_ARCH_ESP32
        bytes=f_size(file_);return true;
#else
        struct stat status{};if(fstat(fileno(file_),&status) || status.st_size<0)return false;
        bytes=static_cast<uint64_t>(status.st_size);return true;
#endif
    }
    bool close(bool retain_on_failure=false) {
        if(!opened_)return true;
#ifdef ARDUINO_ARCH_ESP32
        const bool ok=f_close(file_)==FR_OK;
        if(!ok && retain_on_failure)return false;
#else
        (void)retain_on_failure; // Host fclose consumes the handle even on error.
        const bool ok=std::fclose(file_)==0;
#endif
        // Read errors release the loan. Staging output is only published after
        // sync AND close succeed; a failed staging close also releases its loan.
        opened_=false;release();return ok;
    }
private:
    void acquire() {
        if(opened_)throw std::logic_error("file already open");
        if(!slot_)throw std::invalid_argument("file slot is required");
        if(slot_->used_)throw std::logic_error("file slot already in use");
        slot_->used_=leased_=true;
    }
    void release() {
        file_=nullptr;
        if(leased_) {assert(slot_->used_);slot_->used_=false;leased_=false;}
    }
#ifdef ARDUINO_ARCH_ESP32
    FIL* file_=nullptr;
#else
    std::FILE* file_=nullptr;
#endif
    ReadFileSlot* slot_;
    uint8_t drive_;
    bool opened_=false,leased_=false;
};
} // namespace geoputer

#pragma once
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include "StoragePath.h"
#ifdef ARDUINO_ARCH_ESP32
#include <ff.h>
#else
#include <sys/stat.h>
#endif

namespace geoputer {
class ReadFile;
// Storage owns one slot for the sequential map precheck/index/GLC Readers.
// No other task or persistent log file may use it. The owner outlives all loans.
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
// Read-only file handle, with an optional caller-owned FIL. The default keeps
// the existing owned-FIL behavior for non-map operations and SDK factories.
class ReadFile {
public:
    explicit ReadFile(uint8_t drive=0,ReadFileSlot* slot=nullptr):slot_(slot),drive_(drive){}
    ~ReadFile() {close();}
    ReadFile(const ReadFile&)=delete;
    ReadFile& operator=(const ReadFile&)=delete;
    bool opened()const{return opened_;}
    bool openRead(const std::string& path,NativePathBuffer native_path={}) {
        if(opened_)throw std::logic_error("read file already open");
        if(slot_) {
            if(slot_->used_)throw std::logic_error("read FIL slot already in use");
            slot_->used_=leased_=true;
        }
        try {
#ifdef ARDUINO_ARCH_ESP32
            if(slot_)file_=&slot_->file_;
            else {owned_=std::make_unique<FIL>();file_=owned_.get();}
            opened_=native_path.supplied()
                ?f_open(file_,fatFsPath(drive_,path,native_path),FA_READ)==FR_OK
                :f_open(file_,fatFsPath(drive_,path).c_str(),FA_READ)==FR_OK;
#else
            (void)native_path;
            file_=std::fopen(path.c_str(),"rb");opened_=file_!=nullptr;
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
    bool size(uint64_t& bytes)const {
#ifdef ARDUINO_ARCH_ESP32
        bytes=f_size(file_);return true;
#else
        struct stat status{};if(fstat(fileno(file_),&status) || status.st_size<0)return false;
        bytes=static_cast<uint64_t>(status.st_size);return true;
#endif
    }
    bool close() {
        if(!opened_)return true;
#ifdef ARDUINO_ARCH_ESP32
        const bool ok=f_close(file_)==FR_OK;
#else
        const bool ok=std::fclose(file_)==0;
#endif
        // Read-only files have no deferred application writes. As in the
        // previous Reader, a removed medium must not strand a workspace loan.
        opened_=false;release();return ok;
    }
private:
    void release() {
#ifdef ARDUINO_ARCH_ESP32
        file_=nullptr;owned_.reset();
#else
        file_=nullptr;
#endif
        if(leased_) {assert(slot_->used_);slot_->used_=false;leased_=false;}
    }
#ifdef ARDUINO_ARCH_ESP32
    std::unique_ptr<FIL> owned_;
    FIL* file_=nullptr;
#else
    std::FILE* file_=nullptr;
#endif
    ReadFileSlot* slot_;
    uint8_t drive_;
    bool opened_=false,leased_=false;
};
} // namespace geoputer

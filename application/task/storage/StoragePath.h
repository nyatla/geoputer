#pragma once
#include "../../Config.h"
#include "../../DatasetId.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <string>

namespace geoputer {
// Operation-local UTF-8 byte storage. No heap allocation or silent truncation.
// Keep the object alive until the filesystem call consuming c_str() returns.
class TemporaryPath {
public:
    TemporaryPath() {bytes_[0]='\0';}
    explicit TemporaryPath(std::string_view path):TemporaryPath({path}) {}
    TemporaryPath(std::initializer_list<std::string_view> parts) {
        size_t size=0;
        for(const auto part:parts) {
            if(part.size()>bytes_.size()-1-size)throw std::length_error("Temporary path exceeds 255 bytes");
            if(part.find('\0')!=std::string_view::npos)throw std::invalid_argument("Path contains NUL");
            if(!part.empty())std::memcpy(bytes_.data()+size,part.data(),part.size());
            size+=part.size();
        }
        bytes_[size]='\0';
    }
    static bool fits(std::initializer_list<std::string_view> parts)noexcept {
        size_t size=0;
        for(const auto part:parts) {
            if(part.size()>config::TemporaryPathBytes-1-size || part.find('\0')!=std::string_view::npos)return false;
            size+=part.size();
        }
        return true;
    }
    const char* c_str()const {return bytes_.data();}
    std::string_view view()const {return c_str();}
private:
    std::array<char,config::TemporaryPathBytes> bytes_;
};
static_assert(sizeof(TemporaryPath)==config::TemporaryPathBytes,"Temporary paths contain only their fixed byte buffer");

inline TemporaryPath datasetDirectory(std::string_view id,std::string_view root=config::DatasetRoot) {
    if(!DatasetId::valid(id) || root.empty())throw std::invalid_argument("Invalid dataset ID/root");
    return TemporaryPath({root,root.back()=='/'?"":"/",id});
}

// Non-owning recipe. Full paths support the SDK/file adapters; dataset paths
// refer to the session's fixed filename and the immutable filesystem root.
class FileReadPath {
public:
    FileReadPath(std::string_view full):root_(full) {}
    FileReadPath(const std::string& full):root_(full) {}
    FileReadPath(const char* full):root_(full) {}
    static FileReadPath dataset(std::string_view id,std::string_view name,std::string_view root=config::DatasetRoot) {
        if(!DatasetId::valid(id) || root.empty())throw std::invalid_argument("Invalid dataset ID/root");
        FileReadPath path(root);path.id_=id;path.name_=name;return path;
    }
    TemporaryPath resolve()const {
        if(id_.empty())return TemporaryPath(root_);
        return TemporaryPath({root_,root_.back()=='/'?"":"/",id_,"/",name_});
    }
    TemporaryPath native(uint8_t drive)const {
        if((id_.empty() && root_.substr(0,4)!="/sd/") ||
           (!id_.empty() && root_!="/sd" && root_.substr(0,4)!="/sd/"))
            throw std::runtime_error("SD path is outside /sd");
        char drive_digits[3];const size_t count=drive>=100?3:drive>=10?2:1;
        unsigned value=drive;
        for(size_t n=count;n;value/=10)drive_digits[--n]=static_cast<char>('0'+value%10);
        const auto prefix=std::string_view(drive_digits,count);
        if(id_.empty()) {
            if(root_.size()>=config::TemporaryPathBytes)throw std::length_error("SD path exceeds 255 bytes");
            return TemporaryPath({prefix,":",root_.substr(3)});
        }
        const auto folder=id_,separator=std::string_view(root_.back()=='/'?"":"/");
        if(!TemporaryPath::fits({root_,separator,folder,"/",name_}))throw std::length_error("SD path exceeds 255 bytes");
        return TemporaryPath({prefix,":",root_.substr(3),separator,folder,"/",name_});
    }
private:
    std::string_view root_,name_,id_;
};
inline TemporaryPath fatFsPath(uint8_t drive,FileReadPath path) {return path.native(drive);}
inline TemporaryPath storageNativePath(uint8_t drive,FileReadPath path) {
#ifdef ARDUINO_ARCH_ESP32
    return path.native(drive);
#else
    (void)drive;return path.resolve();
#endif
}
} // namespace geoputer

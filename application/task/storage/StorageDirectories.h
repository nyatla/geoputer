#pragma once
#include "../../Config.h"
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <sys/stat.h>

namespace geoputer::storage_directories {
enum class State { Ready, Missing, Error };
inline constexpr const char* Suffixes[] = {"", "/dataset", "/logs", "/export"};

inline State checkOne(const char* path) {
    struct stat info{};
    if (::stat(path,&info)==0) return S_ISDIR(info.st_mode)?State::Ready:State::Error;
    return errno==ENOENT?State::Missing:State::Error;
}
inline bool pathFor(const char* root,const char* suffix,char (&path)[config::TemporaryPathBytes]) {
    const int length=std::snprintf(path,sizeof(path),"%s%s",root,suffix);
    return length>=0 && static_cast<size_t>(length)<sizeof(path);
}
inline State check(const char* root=config::DataRoot) {
    bool missing=false;
    for(const char* suffix:Suffixes) {
        char path[config::TemporaryPathBytes];
        if(!pathFor(root,suffix,path))return State::Error;
        const auto state=checkOne(path);
        if(state==State::Error)return state;
        missing|=state==State::Missing;
    }
    return missing?State::Missing:State::Ready;
}
inline bool create(const char* root=config::DataRoot) {
    for(const char* suffix:Suffixes) {
        char path[config::TemporaryPathBytes];
        if(!pathFor(root,suffix,path))return false;
        const auto state=checkOne(path);
        if(state==State::Error)return false;
        if(state==State::Missing && ::mkdir(path,0775)!=0)return false;
    }
    return check(root)==State::Ready;
}
} // namespace geoputer::storage_directories

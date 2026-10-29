#pragma once
#include "../StoragePath.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cerrno>
#include <cctype>

namespace geoputer {
// Firmware borrows the ROM root. Host fixture roots must outlive this object.
// Keep path frames out of header/record processing, including after inlining.
class LogDirectory {
public:
    explicit LogDirectory(std::string_view root=config::LogDirectory):root_(root) {}
    bool valid()const {return TemporaryPath::fits({root_});}
    bool fits(std::string_view name)const {return TemporaryPath::fits({root_,"/",name});}
    TemporaryPath path(std::string_view name)const {return TemporaryPath({root_,"/",name});}
    [[gnu::noinline]] DIR* open()const {return opendir(TemporaryPath(root_).c_str());}
    [[gnu::noinline]] bool ensure()const {return !mkdir(TemporaryPath(root_).c_str(),0775) || errno==EEXIST;}
    [[gnu::noinline]] FILE* openStream(std::string_view name)const {return std::fopen(path(name).c_str(),"rb");}
    [[gnu::noinline]] int stat(std::string_view name,struct stat& result)const {return ::stat(path(name).c_str(),&result);}
    [[gnu::noinline]] int remove(std::string_view name)const {return std::remove(path(name).c_str());}
    template<class File> [[gnu::noinline]] bool openRead(File& file,std::string_view name)const {
        return file.openRead(path(name).view());
    }
    template<class File> [[gnu::noinline]] bool openWrite(File& file,std::string_view name,bool exclusive)const {
        return file.open(path(name).view(),exclusive);
    }
    static bool equal(std::string_view a,std::string_view b,bool fold=false) {
        if(a.size()!=b.size())return false;
        for(size_t i=0;i<a.size();++i) {
            const auto x=static_cast<unsigned char>(a[i]),y=static_cast<unsigned char>(b[i]);
            if(fold?std::tolower(x)!=std::tolower(y):x!=y)return false;
        }
        return true;
    }
    bool same(const LogDirectory& other,bool fold=false)const {return equal(root_,other.root_,fold);}
private:
    std::string_view root_;
};
} // namespace geoputer

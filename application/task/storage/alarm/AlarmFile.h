#pragma once
#include "../../../alarm/AlarmTypes.h"
#include "../ReadFile.h"
#include "../StorageActivity.h"
#include <cerrno>
namespace geoputer {
// One small on-disk profile. All FIL/stdio handles are short-lived slot loans.
class AlarmFile {
public:
    static constexpr size_t Bytes=64;
    AlarmFile(uint8_t drive,ReadFileSlot& slot,StorageObserver* observer=nullptr,
              std::string_view root=config::DatasetRoot):slot_(slot),root_(root),drive_(drive),observer_(observer){}
    alarm::Profile load(std::string_view id,uint8_t depth,uint32_t identity);
    bool save(const alarm::Profile& profile,std::string_view id);
    static void encode(const alarm::Profile&,uint8_t (&out)[Bytes]);
    static bool decode(const uint8_t (&bytes)[Bytes],alarm::Profile& expected);
private:
    // 1 exists, 0 absent, -1 inaccessible. Never delete a file on a read error.
    int exists(FileReadPath)const;
    bool remove(FileReadPath)const;
    bool rename(FileReadPath,FileReadPath)const;
    FileReadPath path(std::string_view id,const char* name)const {return FileReadPath::dataset(id,name,root_);}
    ReadFileSlot& slot_;
    std::string_view root_;
    uint8_t drive_;
    StorageObserver* observer_;
};
} // namespace geoputer

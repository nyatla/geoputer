#pragma once
#include "../task/storage/StorageFs.h"
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace geoputer::wifi {
// SD logical paths are decoded once by the HTTP adapter. All operations validate
// them again here, including the private recovery directory and FAT aliases.
bool validPath(std::string_view path,bool root=true);
bool decodePath(std::string_view encoded,char* out,size_t bytes);
bool textExtension(const char* path);
class Utf8Text {
public:
    bool accept(const void* data,size_t bytes);
    bool complete()const{return remaining_==0 && good_;}
private:
    uint32_t code_=0,min_=0;
    unsigned remaining_=0;
    bool good_=true;
};
// One Storage-owned FIL and transfer buffer. No file-sized allocations. A single
// persistent replacement journal recovers interrupted overwrites on next boot.
class Files {
public:
    void attach(uint8_t drive){drive_=drive;}
    bool recover();
    FRESULT stat(const char* path,FILINFO& info);
    bool mkdir(const char* path);
    bool remove(const char* path,bool(*cancelled)(void*)=nullptr,void* context=nullptr);
    bool openRead(const char* path);
    bool read(void* out,unsigned bytes,unsigned& got);
    bool close();
    bool inspectText(const char* path,char version[17]);
    bool beginWrite(const char* path,uint32_t size,bool replace,const char* version=nullptr,bool text=false);
    bool write(const void* data,unsigned bytes);
    bool finish();
    void abort();
    const char* error()const{return error_;}
    uint32_t size()const{return open_?static_cast<uint32_t>(f_size(&file_)):0;}
    uint8_t* buffer(){return buffer_;}
    bool native(const char* logical,char* out,size_t bytes)const;
    static bool visible(const char* name);
private:
    bool fail(const char* reason){error_=reason;return false;}
    bool privatePath(const char* name,char* out,size_t bytes)const;
    bool erasePrivate(const char* name);
    bool journal();
    bool existsNative(const char* path);
    FIL file_{};
    alignas(4) uint8_t buffer_[config::WifiTransferBytes]{};
    char target_[256]{};
    const char* error_="";
    uint32_t expected_=0,written_=0;
    uint8_t drive_=0xff;
    bool open_=false,writing_=false,text_=false;
    Utf8Text utf8_;
};
}

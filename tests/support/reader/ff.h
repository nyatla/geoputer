#pragma once
// Read-only FatFs adapter backed by real host files, with injectable failures.
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <sys/stat.h>
using BYTE=uint8_t;
using UINT=unsigned;
using FSIZE_t=uint32_t;
enum FRESULT {FR_OK,FR_DISK_ERR,FR_NO_FILE,FR_INVALID_DRIVE,FR_DENIED};
constexpr BYTE FA_READ=1, AM_DIR=0x10;
struct FIL {std::FILE* handle=nullptr;FSIZE_t fptr=0,size=0;};
struct FILINFO {FSIZE_t fsize=0;BYTE fattrib=0;};
namespace fatfs_test {
inline bool fail_open=false,fail_stat=false,fail_seek=false,fail_read=false;
inline unsigned live=0,opens=0,closes=0,seeks=0,reads=0;
inline unsigned drive=0;
inline std::string last_open,last_stat;
inline std::string native(const char* path) {
    const std::string prefix=std::to_string(drive)+":";
    const std::string value=path;
    return value.compare(0,prefix.size(),prefix)==0 ? value.substr(prefix.size()) : "";
}
}
inline FRESULT f_stat(const char* path,FILINFO* info) {
    fatfs_test::last_stat=path;
    const auto native=fatfs_test::native(path);
    if(native.empty())return FR_INVALID_DRIVE;
    struct stat st{};
    if(fatfs_test::fail_stat || stat(native.c_str(),&st))return FR_NO_FILE;
    info->fsize=static_cast<FSIZE_t>(st.st_size);
    info->fattrib=S_ISDIR(st.st_mode)?AM_DIR:0;
    return FR_OK;
}
inline FRESULT f_open(FIL* file,const char* path,BYTE mode) {
    fatfs_test::last_open=path;
    const auto native=fatfs_test::native(path);
    if(native.empty())return FR_INVALID_DRIVE;
    if(mode!=FA_READ || fatfs_test::fail_open)return FR_DENIED;
    file->handle=std::fopen(native.c_str(),"rb");
    if(!file->handle)return FR_NO_FILE;
    struct stat st{};
    if(fstat(fileno(file->handle),&st)){std::fclose(file->handle);file->handle=nullptr;return FR_DISK_ERR;}
    file->size=static_cast<FSIZE_t>(st.st_size);file->fptr=0;
    ++fatfs_test::live;++fatfs_test::opens;return FR_OK;
}
inline FRESULT f_close(FIL* file) {
    const int result=std::fclose(file->handle);file->handle=nullptr;
    --fatfs_test::live;++fatfs_test::closes;return result?FR_DISK_ERR:FR_OK;
}
inline FRESULT f_lseek(FIL* file,FSIZE_t offset) {
    ++fatfs_test::seeks;
    if(fatfs_test::fail_seek)return FR_DISK_ERR;
    // FatFs read-only seeks past EOF succeed, but clip the position.
    offset=std::min(offset,file->size);
    if(fseeko(file->handle,offset,SEEK_SET))return FR_DISK_ERR;
    file->fptr=offset;return FR_OK;
}
inline FSIZE_t f_tell(const FIL* file){return file->fptr;}
inline FRESULT f_read(FIL* file,void* buffer,UINT count,UINT* received) {
    ++fatfs_test::reads;
    if(fatfs_test::fail_read)count=std::min(count,3u);
    *received=static_cast<UINT>(std::fread(buffer,1,count,file->handle));
    file->fptr+=*received;
    return fatfs_test::fail_read || std::ferror(file->handle)?FR_DISK_ERR:FR_OK;
}

#pragma once
#include <cstdint>
#include <functional>
#include <array>
namespace usb_test {
inline bool driver=false, registered=false, mounted=false, sync_error=false, init_error=false;
inline unsigned mounts=0, uninitializations=0, reads=0, writes=0;
inline std::function<void()> on_read;
inline std::array<uint8_t,1536> disk{};
}
using DWORD=uint32_t;
constexpr int FR_OK=0,FS_FAT32=3;
struct FATFS {int fs_type=FS_FAT32;};
inline FATFS usb_test_fs;
inline int f_getfree(const char*,DWORD* free,FATFS** fs) {*free=1;*fs=&usb_test_fs;return usb_test::mounted?FR_OK:1;}
inline int f_mount(FATFS*,const char*,int) {usb_test::mounted=false;return FR_OK;}

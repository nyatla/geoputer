#pragma once
#include "../../Config.h"
#include <ff.h>
#if defined(ARDUINO_ARCH_ESP32) && !defined(GEOPUTER_TEST_FATFS)
#include <SD_FatFs.h>
namespace geoputer {
// All application native file operations use this one backend. The low-level
// Arduino SD driver keeps its independent SDK disk-I/O registry.
static_assert(FF_DEFINED==SD_FATFS_FF_DEFINED,"FatFs accessor/flag compatibility");
using FIL=SD_FATFS_FIL;
using FILINFO=SD_FATFS_FILINFO;
using FATFS=SD_FATFS_FATFS;
using FRESULT=SD_FATFS_FRESULT;
using FF_DIR=SD_FATFS_FF_DIR;
using FSIZE_t=SD_FATFS_FSIZE_t;
inline constexpr auto f_open=::sd_fatfs_f_open;
inline constexpr auto f_close=::sd_fatfs_f_close;
inline constexpr auto f_read=::sd_fatfs_f_read;
inline constexpr auto f_write=::sd_fatfs_f_write;
inline constexpr auto f_lseek=::sd_fatfs_f_lseek;
inline constexpr auto f_sync=::sd_fatfs_f_sync;
inline constexpr auto f_stat=::sd_fatfs_f_stat;
inline constexpr auto f_unlink=::sd_fatfs_f_unlink;
inline constexpr auto f_rename=::sd_fatfs_f_rename;
inline constexpr auto f_mount=::sd_fatfs_f_mount;
inline constexpr auto f_getfree=::sd_fatfs_f_getfree;
inline constexpr auto f_mkfs=::sd_fatfs_f_mkfs;
inline constexpr auto f_opendir=::sd_fatfs_f_opendir;
inline constexpr auto f_closedir=::sd_fatfs_f_closedir;
inline constexpr auto f_readdir=::sd_fatfs_f_readdir;
inline constexpr auto FR_OK=SD_FATFS_FR_OK;
inline constexpr auto FR_DISK_ERR=SD_FATFS_FR_DISK_ERR;
inline constexpr auto FR_INT_ERR=SD_FATFS_FR_INT_ERR;
inline constexpr auto FR_NOT_READY=SD_FATFS_FR_NOT_READY;
inline constexpr auto FR_NO_FILE=SD_FATFS_FR_NO_FILE;
inline constexpr auto FR_NO_PATH=SD_FATFS_FR_NO_PATH;
inline constexpr auto FR_INVALID_NAME=SD_FATFS_FR_INVALID_NAME;
inline constexpr auto FR_DENIED=SD_FATFS_FR_DENIED;
inline constexpr auto FR_EXIST=SD_FATFS_FR_EXIST;
inline constexpr auto FR_INVALID_OBJECT=SD_FATFS_FR_INVALID_OBJECT;
inline constexpr auto FR_WRITE_PROTECTED=SD_FATFS_FR_WRITE_PROTECTED;
inline constexpr auto FR_INVALID_DRIVE=SD_FATFS_FR_INVALID_DRIVE;
inline constexpr auto FR_NOT_ENABLED=SD_FATFS_FR_NOT_ENABLED;
inline constexpr auto FR_NO_FILESYSTEM=SD_FATFS_FR_NO_FILESYSTEM;
inline constexpr auto FR_MKFS_ABORTED=SD_FATFS_FR_MKFS_ABORTED;
inline constexpr auto FR_TIMEOUT=SD_FATFS_FR_TIMEOUT;
inline constexpr auto FR_LOCKED=SD_FATFS_FR_LOCKED;
inline constexpr auto FR_NOT_ENOUGH_CORE=SD_FATFS_FR_NOT_ENOUGH_CORE;
inline constexpr auto FR_TOO_MANY_OPEN_FILES=SD_FATFS_FR_TOO_MANY_OPEN_FILES;
inline constexpr auto FR_INVALID_PARAMETER=SD_FATFS_FR_INVALID_PARAMETER;
// f_size/f_tell and FA_*/FM_*/FS_* are SDK macros. R0.13c defines them
// as field accessors or numeric constants, valid for both independent types.
static_assert(FA_READ==SD_FATFS_FA_READ && FA_WRITE==SD_FATFS_FA_WRITE &&
              FA_CREATE_NEW==SD_FATFS_FA_CREATE_NEW && FS_FAT32==SD_FATFS_FS_FAT32,
              "FatFs flags differ");
}
#endif

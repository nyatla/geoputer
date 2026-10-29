/* SD_FatFs configuration. Independent of sdkconfig.h and SDK ffconf.h. */
#ifndef SD_FATFS_CONFIG_INCLUDED
#define SD_FATFS_CONFIG_INCLUDED
#define SD_FATFS_FFCONF_DEF 86604
#define SD_FATFS_FF_FS_READONLY 0
#define SD_FATFS_FF_FS_MINIMIZE 0
#define SD_FATFS_FF_USE_STRFUNC 0
#define SD_FATFS_FF_USE_FIND 0
#define SD_FATFS_FF_USE_MKFS 1
#define SD_FATFS_FF_USE_FASTSEEK 0
#define SD_FATFS_FF_USE_EXPAND 0
#define SD_FATFS_FF_USE_CHMOD 1
#define SD_FATFS_FF_USE_LABEL 0
#define SD_FATFS_FF_USE_FORWARD 0
#define SD_FATFS_FF_CODE_PAGE 850
#define SD_FATFS_FF_USE_LFN 2
#define SD_FATFS_FF_MAX_LFN 255
#define SD_FATFS_FF_LFN_UNICODE 2
#define SD_FATFS_FF_LFN_BUF 255
#define SD_FATFS_FF_SFN_BUF 12
#define SD_FATFS_FF_STRF_ENCODE 3
#define SD_FATFS_FF_FS_RPATH 0
#define SD_FATFS_FF_VOLUMES 2
#define SD_FATFS_FF_STR_VOLUME_ID 0
#define SD_FATFS_FF_MULTI_PARTITION 1
#define SD_FATFS_FF_MIN_SS 512
#define SD_FATFS_FF_MAX_SS 512
#define SD_FATFS_FF_USE_TRIM 0
#define SD_FATFS_FF_FS_NOFSINFO 0
#define SD_FATFS_FF_FS_TINY 0
#define SD_FATFS_FF_FS_EXFAT 0
#define SD_FATFS_FF_FS_NORTC 0
#define SD_FATFS_FF_NORTC_MON 1
#define SD_FATFS_FF_NORTC_MDAY 1
#define SD_FATFS_FF_NORTC_YEAR 1980
#define SD_FATFS_FF_FS_LOCK 0
#define SD_FATFS_FF_FS_REENTRANT 1
/* Milliseconds, converted by the caller's lock callback. */
#define SD_FATFS_FF_FS_TIMEOUT 10000
#define SD_FATFS_FF_SYNC_t void*
/* ESP-IDF R0.13c calls this with NULL on insufficient mkfs workspace. */
void sd_fatfs_ff_memfree(void* ptr);
#endif

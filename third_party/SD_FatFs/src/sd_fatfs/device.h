/* Device adapter for the isolated 512-byte-sector FatFs instance. */
#ifndef SD_FATFS_DEVICE_H
#define SD_FATFS_DEVICE_H
#include "ff.h"
#include "diskio.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void* context;
    SD_FATFS_DSTATUS (*initialize)(void* context);
    SD_FATFS_DSTATUS (*status)(void* context);
    /* Transfer callbacks detect removal/I/O failure/write protection themselves.
     * The adapter does not call status again before read/write/ioctl. */
    SD_FATFS_DRESULT (*read)(void* context, SD_FATFS_BYTE* buffer,
                            SD_FATFS_DWORD sector, SD_FATFS_UINT count);
    SD_FATFS_DRESULT (*write)(void* context, const SD_FATFS_BYTE* buffer,
                             SD_FATFS_DWORD sector, SD_FATFS_UINT count);
    SD_FATFS_DRESULT (*ioctl)(void* context, SD_FATFS_BYTE command, void* buffer);
    /* Mandatory: lock the device until unlock, returning 0 on timeout. */
    int (*lock)(void* context, SD_FATFS_DWORD timeout_ms);
    void (*unlock)(void* context);
} SD_FATFS_Device;

/* Copies callbacks. Context and the device lock must outlive the binding.
 * Call bind/unbind, mount/unmount and clock setup from one controlling task.
 * Unbind rejects volumes still registered by f_mount, even failed mounts. */
SD_FATFS_FRESULT sd_fatfs_bind_device(SD_FATFS_BYTE drive, const SD_FATFS_Device* device);
SD_FATFS_FRESULT sd_fatfs_unbind_device(SD_FATFS_BYTE drive);
/* FAT local timestamp, identical bit layout to FatFs get_fattime.
 * NULL selects the fixed fallback 1980-01-01 00:00:00. */
void sd_fatfs_set_clock(SD_FATFS_DWORD (*clock)(void*), void* context);

#ifdef __cplusplus
}
#endif
#endif

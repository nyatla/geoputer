/* ESP-IDF 4.4 VFS adapter for the independent SD FatFs instance. */
#ifndef SD_FATFS_ESP32_VFS_H
#define SD_FATFS_ESP32_VFS_H
#ifdef ESP_PLATFORM
#include "ff.h"
#include "esp_err.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Caller mounts the returned FS separately. All descriptors must be closed
 * and the volume unmounted before unregistering the VFS path. */
esp_err_t sd_fatfs_vfs_register(const char* base_path,const char* fat_drive,
                               size_t max_files,SD_FATFS_FATFS** out_fs);
esp_err_t sd_fatfs_vfs_unregister_path(const char* base_path);
#ifdef __cplusplus
}
#endif
#endif
#endif

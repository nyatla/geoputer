/* SD_FatFs device adapter. Distributed under the accompanying LICENSE. */
#include "device.h"
#include <string.h>

typedef struct {
    SD_FATFS_Device device;
    unsigned int mounted;
    int bound;
    int initialized;
} DeviceSlot;
static DeviceSlot devices[SD_FATFS_FF_VOLUMES];
static SD_FATFS_DWORD (*clock_source)(void*);
static void* clock_context;
SD_FATFS_PARTITION SD_FATFS_VolToPart[SD_FATFS_FF_VOLUMES] = {{0, 0}, {1, 0}};

static DeviceSlot* slot(SD_FATFS_BYTE drive) {
    return drive < SD_FATFS_FF_VOLUMES && devices[drive].bound ? &devices[drive] : NULL;
}
SD_FATFS_FRESULT sd_fatfs_bind_device(SD_FATFS_BYTE drive, const SD_FATFS_Device* device) {
    if (drive >= SD_FATFS_FF_VOLUMES) return SD_FATFS_FR_INVALID_DRIVE;
    if (!device || !device->initialize || !device->status || !device->read ||
        !device->write || !device->ioctl || !device->lock || !device->unlock)
        return SD_FATFS_FR_INVALID_PARAMETER;
    if (devices[drive].bound) return SD_FATFS_FR_EXIST;
    devices[drive].device = *device;
    devices[drive].bound = 1;
    return SD_FATFS_FR_OK;
}
SD_FATFS_FRESULT sd_fatfs_unbind_device(SD_FATFS_BYTE drive) {
    DeviceSlot* s = slot(drive);
    if (!s) return SD_FATFS_FR_INVALID_DRIVE;
    if (s->mounted) return SD_FATFS_FR_LOCKED;
    memset(s, 0, sizeof(*s));
    return SD_FATFS_FR_OK;
}
SD_FATFS_DSTATUS sd_fatfs_disk_status(SD_FATFS_BYTE drive) {
    DeviceSlot* s = slot(drive);
    if (!s) return SD_FATFS_STA_NOINIT | SD_FATFS_STA_NODISK;
    return s->device.status(s->device.context) | (s->initialized ? 0 : SD_FATFS_STA_NOINIT);
}
SD_FATFS_DSTATUS sd_fatfs_disk_initialize(SD_FATFS_BYTE drive) {
    DeviceSlot* s = slot(drive);
    SD_FATFS_WORD sector_size = 0;
    SD_FATFS_DSTATUS status;
    if (!s) return SD_FATFS_STA_NOINIT | SD_FATFS_STA_NODISK;
    s->initialized = 0;
    status = s->device.initialize(s->device.context);
    if (status & SD_FATFS_STA_NOINIT) return status;
    if (s->device.ioctl(s->device.context, SD_FATFS_GET_SECTOR_SIZE, &sector_size) != SD_FATFS_RES_OK ||
        sector_size != SD_FATFS_FF_MAX_SS) return status | SD_FATFS_STA_NOINIT;
    s->initialized = 1;
    return status;
}
// FatFs validates media status at its API boundary. Do not repeat physical
// status queries for every transfer; the backend reports transfer-time errors.
SD_FATFS_DRESULT sd_fatfs_disk_read(SD_FATFS_BYTE drive, SD_FATFS_BYTE* buffer,
                                   SD_FATFS_DWORD sector, SD_FATFS_UINT count) {
    DeviceSlot* s = slot(drive);
    if (!buffer || !count) return SD_FATFS_RES_PARERR;
    if (!s || !s->initialized) return SD_FATFS_RES_NOTRDY;
    return s->device.read(s->device.context, buffer, sector, count);
}
SD_FATFS_DRESULT sd_fatfs_disk_write(SD_FATFS_BYTE drive, const SD_FATFS_BYTE* buffer,
                                    SD_FATFS_DWORD sector, SD_FATFS_UINT count) {
    DeviceSlot* s = slot(drive);
    if (!buffer || !count) return SD_FATFS_RES_PARERR;
    if (!s || !s->initialized) return SD_FATFS_RES_NOTRDY;
    return s->device.write(s->device.context, buffer, sector, count);
}
SD_FATFS_DRESULT sd_fatfs_disk_ioctl(SD_FATFS_BYTE drive, SD_FATFS_BYTE command, void* buffer) {
    DeviceSlot* s = slot(drive);
    if (!s || !s->initialized) return SD_FATFS_RES_NOTRDY;
    return s->device.ioctl(s->device.context, command, buffer);
}
int sd_fatfs_ff_cre_syncobj(SD_FATFS_BYTE volume, SD_FATFS_FF_SYNC_t* object) {
    DeviceSlot* s;
    if (volume >= SD_FATFS_FF_VOLUMES) return 0;
    s = slot(SD_FATFS_VolToPart[volume].pd);
    if (!s) return 0;
    ++s->mounted;
    *object = s;
    return 1;
}
int sd_fatfs_ff_req_grant(SD_FATFS_FF_SYNC_t object) {
    DeviceSlot* s = (DeviceSlot*)object;
    return s->device.lock(s->device.context, SD_FATFS_FF_FS_TIMEOUT);
}
void sd_fatfs_ff_rel_grant(SD_FATFS_FF_SYNC_t object) {
    DeviceSlot* s = (DeviceSlot*)object;
    s->device.unlock(s->device.context);
}
int sd_fatfs_ff_del_syncobj(SD_FATFS_FF_SYNC_t object) {
    DeviceSlot* s = (DeviceSlot*)object;
    --s->mounted;
    return 1;
}
void sd_fatfs_set_clock(SD_FATFS_DWORD (*clock)(void*), void* context) {
    clock_source = clock;
    clock_context = context;
}
SD_FATFS_DWORD sd_fatfs_get_fattime(void) {
    return clock_source ? clock_source(clock_context) : (1u << 21) | (1u << 16);
}
/* LFN=stack: the pinned ESP-IDF error path only calls this with NULL. */
void sd_fatfs_ff_memfree(void* pointer) { (void)pointer; }

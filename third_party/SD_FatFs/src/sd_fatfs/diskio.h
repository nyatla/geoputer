/*-----------------------------------------------------------------------/
/  Low level disk interface modlue include file   (C)ChaN, 2014          /
/-----------------------------------------------------------------------*/

#ifndef SD_FATFS_DISKIO_DEFINED
#define SD_FATFS_DISKIO_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

/* Status of Disk Functions */
typedef SD_FATFS_BYTE	SD_FATFS_DSTATUS;

/* Results of Disk Functions */
typedef enum {
	SD_FATFS_RES_OK = 0,		/* 0: Successful */
	SD_FATFS_RES_ERROR,		/* 1: R/W Error */
	SD_FATFS_RES_WRPRT,		/* 2: Write Protected */
	SD_FATFS_RES_NOTRDY,		/* 3: Not Ready */
	SD_FATFS_RES_PARERR		/* 4: Invalid Parameter */
} SD_FATFS_DRESULT;


/*---------------------------------------*/
/* Prototypes for disk control functions */


SD_FATFS_DSTATUS sd_fatfs_disk_initialize (SD_FATFS_BYTE pdrv);
SD_FATFS_DSTATUS sd_fatfs_disk_status (SD_FATFS_BYTE pdrv);
SD_FATFS_DRESULT sd_fatfs_disk_read (SD_FATFS_BYTE pdrv, SD_FATFS_BYTE* buff, SD_FATFS_DWORD sector, SD_FATFS_UINT count);
SD_FATFS_DRESULT sd_fatfs_disk_write (SD_FATFS_BYTE pdrv, const SD_FATFS_BYTE* buff, SD_FATFS_DWORD sector, SD_FATFS_UINT count);
SD_FATFS_DRESULT sd_fatfs_disk_ioctl (SD_FATFS_BYTE pdrv, SD_FATFS_BYTE cmd, void* buff);


/* Disk Status Bits (DSTATUS) */

#define SD_FATFS_STA_NOINIT		0x01	/* Drive not initialized */
#define SD_FATFS_STA_NODISK		0x02	/* No medium in the drive */
#define SD_FATFS_STA_PROTECT		0x04	/* Write protected */


/* Command code for disk_ioctrl fucntion */

/* Generic command (Used by FatFs) */
#define SD_FATFS_CTRL_SYNC			0	/* Complete pending write process (needed at FF_FS_READONLY == 0) */
#define SD_FATFS_GET_SECTOR_COUNT	1	/* Get media size (needed at FF_USE_MKFS == 1) */
#define SD_FATFS_GET_SECTOR_SIZE		2	/* Get sector size (needed at FF_MAX_SS != FF_MIN_SS) */
#define SD_FATFS_GET_BLOCK_SIZE		3	/* Get erase block size (needed at FF_USE_MKFS == 1) */
#define SD_FATFS_CTRL_TRIM			4	/* Inform device that the data on the block of sectors is no longer used (needed at FF_USE_TRIM == 1) */

/* Generic command (Not used by FatFs) */
#define SD_FATFS_CTRL_POWER			5	/* Get/Set power status */
#define SD_FATFS_CTRL_LOCK			6	/* Lock/Unlock media removal */
#define SD_FATFS_CTRL_EJECT			7	/* Eject media */
#define SD_FATFS_CTRL_FORMAT			8	/* Create physical format on the media */

/* MMC/SDC specific ioctl command */
#define SD_FATFS_MMC_GET_TYPE		10	/* Get card type */
#define SD_FATFS_MMC_GET_CSD			11	/* Get CSD */
#define SD_FATFS_MMC_GET_CID			12	/* Get CID */
#define SD_FATFS_MMC_GET_OCR			13	/* Get OCR */
#define SD_FATFS_MMC_GET_SDSTAT		14	/* Get SD status */
#define SD_FATFS_ISDIO_READ			55	/* Read data form SD iSDIO register */
#define SD_FATFS_ISDIO_WRITE			56	/* Write data to SD iSDIO register */
#define SD_FATFS_ISDIO_MRITE			57	/* Masked write data to SD iSDIO register */

/* ATA/CF specific ioctl command */
#define SD_FATFS_ATA_GET_REV			20	/* Get F/W revision */
#define SD_FATFS_ATA_GET_MODEL		21	/* Get model name */
#define SD_FATFS_ATA_GET_SN			22	/* Get serial number */

#ifdef __cplusplus
}
#endif

#endif

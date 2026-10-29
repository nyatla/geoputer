/*----------------------------------------------------------------------------/
/  FatFs - Generic FAT Filesystem module  R0.13c                              /
/-----------------------------------------------------------------------------/
/
/ Copyright (C) 2018, ChaN, all right reserved.
/
/ FatFs module is an open source software. Redistribution and use of FatFs in
/ source and binary forms, with or without modification, are permitted provided
/ that the following condition is met:

/ 1. Redistributions of source code must retain the above copyright notice,
/    this condition and the following disclaimer.
/
/ This software is provided by the copyright holder and contributors "AS IS"
/ and any warranties related to this software are DISCLAIMED.
/ The copyright owner or contributors be NOT LIABLE for any damages caused
/ by use of this software.
/
/----------------------------------------------------------------------------*/


#ifndef SD_FATFS_FF_DEFINED
#define SD_FATFS_FF_DEFINED	86604	/* Revision ID */

#ifdef __cplusplus
extern "C" {
#endif

#include "ffconf.h"		/* FatFs configuration options */

#if SD_FATFS_FF_DEFINED != SD_FATFS_FFCONF_DEF
#error Wrong configuration file (ffconf.h).
#endif


/* Integer types used for FatFs API */

#define SD_FATFS_FF_INTDEF 2
#include <stdint.h>
typedef unsigned int SD_FATFS_UINT;
typedef uint8_t SD_FATFS_BYTE;
typedef uint16_t SD_FATFS_WORD;
typedef uint16_t SD_FATFS_WCHAR;
typedef uint32_t SD_FATFS_DWORD;
typedef uint64_t SD_FATFS_QWORD;

/* Definitions of volume management */

#if SD_FATFS_FF_MULTI_PARTITION		/* Multiple partition configuration */
typedef struct {
	SD_FATFS_BYTE pd;	/* Physical drive number */
	SD_FATFS_BYTE pt;	/* Partition: 0:Auto detect, 1-4:Forced partition) */
} SD_FATFS_PARTITION;
extern SD_FATFS_PARTITION SD_FATFS_VolToPart[];	/* Volume - Partition resolution table */
#endif

#if SD_FATFS_FF_STR_VOLUME_ID
#ifndef SD_FATFS_FF_VOLUME_STRS
extern const char* SD_FATFS_VolumeStr[SD_FATFS_FF_VOLUMES];	/* User defied volume ID */
#endif
#endif



/* Type of path name strings on FatFs API */

#ifndef SD_FATFS_INC_TCHAR
#define SD_FATFS_INC_TCHAR

#if SD_FATFS_FF_USE_LFN && SD_FATFS_FF_LFN_UNICODE == 1 	/* Unicode in UTF-16 encoding */
typedef SD_FATFS_WCHAR SD_FATFS_TCHAR;
#define SD_FATFS_T(x) L ## x
#define SD_FATFS_TEXT(x) L ## x
#elif SD_FATFS_FF_USE_LFN && SD_FATFS_FF_LFN_UNICODE == 2	/* Unicode in UTF-8 encoding */
typedef char SD_FATFS_TCHAR;
#define SD_FATFS_T(x) u8 ## x
#define SD_FATFS_TEXT(x) u8 ## x
#elif SD_FATFS_FF_USE_LFN && SD_FATFS_FF_LFN_UNICODE == 3	/* Unicode in UTF-32 encoding */
typedef SD_FATFS_DWORD SD_FATFS_TCHAR;
#define SD_FATFS_T(x) U ## x
#define SD_FATFS_TEXT(x) U ## x
#elif SD_FATFS_FF_USE_LFN && (SD_FATFS_FF_LFN_UNICODE < 0 || SD_FATFS_FF_LFN_UNICODE > 3)
#error Wrong SD_FATFS_FF_LFN_UNICODE setting
#else									/* ANSI/OEM code in SBCS/DBCS */
typedef char SD_FATFS_TCHAR;
#define SD_FATFS_T(x) x
#define SD_FATFS_TEXT(x) x
#endif

#endif



/* Type of file size variables */

#if SD_FATFS_FF_FS_EXFAT
#if SD_FATFS_FF_INTDEF != 2
#error exFAT feature wants C99 or later
#endif
typedef SD_FATFS_QWORD SD_FATFS_FSIZE_t;
#else
typedef SD_FATFS_DWORD SD_FATFS_FSIZE_t;
#endif



/* Filesystem object structure (FATFS) */

typedef struct {
	SD_FATFS_BYTE	fs_type;		/* Filesystem type (0:not mounted) */
	SD_FATFS_BYTE	pdrv;			/* Associated physical drive */
	SD_FATFS_BYTE	n_fats;			/* Number of FATs (1 or 2) */
	SD_FATFS_BYTE	wflag;			/* win[] flag (b0:dirty) */
	SD_FATFS_BYTE	fsi_flag;		/* FSINFO flags (b7:disabled, b0:dirty) */
	SD_FATFS_WORD	id;				/* Volume mount ID */
	SD_FATFS_WORD	n_rootdir;		/* Number of root directory entries (FAT12/16) */
	SD_FATFS_WORD	csize;			/* Cluster size [sectors] */
#if SD_FATFS_FF_MAX_SS != SD_FATFS_FF_MIN_SS
	SD_FATFS_WORD	ssize;			/* Sector size (512, 1024, 2048 or 4096) */
#endif
#if SD_FATFS_FF_USE_LFN
	SD_FATFS_WCHAR*	lfnbuf;			/* LFN working buffer */
#endif
#if SD_FATFS_FF_FS_EXFAT
	SD_FATFS_BYTE*	dirbuf;			/* Directory entry block scratchpad buffer for exFAT */
#endif
#if SD_FATFS_FF_FS_REENTRANT
	SD_FATFS_FF_SYNC_t	sobj;		/* Identifier of sync object */
#endif
#if !SD_FATFS_FF_FS_READONLY
	SD_FATFS_DWORD	last_clst;		/* Last allocated cluster */
	SD_FATFS_DWORD	free_clst;		/* Number of free clusters */
#endif
#if SD_FATFS_FF_FS_RPATH
	SD_FATFS_DWORD	cdir;			/* Current directory start cluster (0:root) */
#if SD_FATFS_FF_FS_EXFAT
	SD_FATFS_DWORD	cdc_scl;		/* Containing directory start cluster (invalid when cdir is 0) */
	SD_FATFS_DWORD	cdc_size;		/* b31-b8:Size of containing directory, b7-b0: Chain status */
	SD_FATFS_DWORD	cdc_ofs;		/* Offset in the containing directory (invalid when cdir is 0) */
#endif
#endif
	SD_FATFS_DWORD	n_fatent;		/* Number of FAT entries (number of clusters + 2) */
	SD_FATFS_DWORD	fsize;			/* Size of an FAT [sectors] */
	SD_FATFS_DWORD	volbase;		/* Volume base sector */
	SD_FATFS_DWORD	fatbase;		/* FAT base sector */
	SD_FATFS_DWORD	dirbase;		/* Root directory base sector/cluster */
	SD_FATFS_DWORD	database;		/* Data base sector */
#if SD_FATFS_FF_FS_EXFAT
	SD_FATFS_DWORD	bitbase;		/* Allocation bitmap base sector */
#endif
	SD_FATFS_DWORD	winsect;		/* Current sector appearing in the win[] */
	SD_FATFS_BYTE	win[SD_FATFS_FF_MAX_SS];	/* Disk access window for Directory, FAT (and file data at tiny cfg) */
} SD_FATFS_FATFS;



/* Object ID and allocation information (FFOBJID) */

typedef struct {
	SD_FATFS_FATFS*	fs;				/* Pointer to the hosting volume of this object */
	SD_FATFS_WORD	id;				/* Hosting volume mount ID */
	SD_FATFS_BYTE	attr;			/* Object attribute */
	SD_FATFS_BYTE	stat;			/* Object chain status (b1-0: =0:not contiguous, =2:contiguous, =3:fragmented in this session, b2:sub-directory stretched) */
	SD_FATFS_DWORD	sclust;			/* Object data start cluster (0:no cluster or root directory) */
	SD_FATFS_FSIZE_t	objsize;		/* Object size (valid when sclust != 0) */
#if SD_FATFS_FF_FS_EXFAT
	SD_FATFS_DWORD	n_cont;			/* Size of first fragment - 1 (valid when stat == 3) */
	SD_FATFS_DWORD	n_frag;			/* Size of last fragment needs to be written to FAT (valid when not zero) */
	SD_FATFS_DWORD	c_scl;			/* Containing directory start cluster (valid when sclust != 0) */
	SD_FATFS_DWORD	c_size;			/* b31-b8:Size of containing directory, b7-b0: Chain status (valid when c_scl != 0) */
	SD_FATFS_DWORD	c_ofs;			/* Offset in the containing directory (valid when file object and sclust != 0) */
#endif
#if SD_FATFS_FF_FS_LOCK
	SD_FATFS_UINT	lockid;			/* File lock ID origin from 1 (index of file semaphore table Files[]) */
#endif
} SD_FATFS_FFOBJID;



/* File object structure (FIL) */

typedef struct {
	SD_FATFS_FFOBJID	obj;			/* Object identifier (must be the 1st member to detect invalid object pointer) */
	SD_FATFS_BYTE	flag;			/* File status flags */
	SD_FATFS_BYTE	err;			/* Abort flag (error code) */
	SD_FATFS_FSIZE_t	fptr;			/* File read/write pointer (Zeroed on file open) */
	SD_FATFS_DWORD	clust;			/* Current cluster of fpter (invalid when fptr is 0) */
	SD_FATFS_DWORD	sect;			/* Sector number appearing in buf[] (0:invalid) */
#if !SD_FATFS_FF_FS_READONLY
	SD_FATFS_DWORD	dir_sect;		/* Sector number containing the directory entry (not used at exFAT) */
	SD_FATFS_BYTE*	dir_ptr;		/* Pointer to the directory entry in the win[] (not used at exFAT) */
#endif
#if SD_FATFS_FF_USE_FASTSEEK
	SD_FATFS_DWORD*	cltbl;			/* Pointer to the cluster link map table (nulled on open, set by application) */
#endif
#if !SD_FATFS_FF_FS_TINY
	SD_FATFS_BYTE	buf[SD_FATFS_FF_MAX_SS];	/* File private data read/write window */
#endif
} SD_FATFS_FIL;



/* Directory object structure (FF_DIR) */

typedef struct {
	SD_FATFS_FFOBJID	obj;			/* Object identifier */
	SD_FATFS_DWORD	dptr;			/* Current read/write offset */
	SD_FATFS_DWORD	clust;			/* Current cluster */
	SD_FATFS_DWORD	sect;			/* Current sector (0:Read operation has terminated) */
	SD_FATFS_BYTE*	dir;			/* Pointer to the directory item in the win[] */
	SD_FATFS_BYTE	fn[12];			/* SFN (in/out) {body[8],ext[3],status[1]} */
#if SD_FATFS_FF_USE_LFN
	SD_FATFS_DWORD	blk_ofs;		/* Offset of current entry block being processed (0xFFFFFFFF:Invalid) */
#endif
#if SD_FATFS_FF_USE_FIND
	const SD_FATFS_TCHAR* pat;		/* Pointer to the name matching pattern */
#endif
} SD_FATFS_FF_DIR;



/* File information structure (FILINFO) */

typedef struct {
	SD_FATFS_FSIZE_t	fsize;			/* File size */
	SD_FATFS_WORD	fdate;			/* Modified date */
	SD_FATFS_WORD	ftime;			/* Modified time */
	SD_FATFS_BYTE	fattrib;		/* File attribute */
#if SD_FATFS_FF_USE_LFN
	SD_FATFS_TCHAR	altname[SD_FATFS_FF_SFN_BUF + 1];/* Altenative file name */
	SD_FATFS_TCHAR	fname[SD_FATFS_FF_LFN_BUF + 1];	/* Primary file name */
#else
	SD_FATFS_TCHAR	fname[12 + 1];	/* File name */
#endif
} SD_FATFS_FILINFO;



/* File function return code (FRESULT) */

typedef enum {
	SD_FATFS_FR_OK = 0,				/* (0) Succeeded */
	SD_FATFS_FR_DISK_ERR,			/* (1) A hard error occurred in the low level disk I/O layer */
	SD_FATFS_FR_INT_ERR,				/* (2) Assertion failed */
	SD_FATFS_FR_NOT_READY,			/* (3) The physical drive cannot work */
	SD_FATFS_FR_NO_FILE,				/* (4) Could not find the file */
	SD_FATFS_FR_NO_PATH,				/* (5) Could not find the path */
	SD_FATFS_FR_INVALID_NAME,		/* (6) The path name format is invalid */
	SD_FATFS_FR_DENIED,				/* (7) Access denied due to prohibited access or directory full */
	SD_FATFS_FR_EXIST,				/* (8) Access denied due to prohibited access */
	SD_FATFS_FR_INVALID_OBJECT,		/* (9) The file/directory object is invalid */
	SD_FATFS_FR_WRITE_PROTECTED,		/* (10) The physical drive is write protected */
	SD_FATFS_FR_INVALID_DRIVE,		/* (11) The logical drive number is invalid */
	SD_FATFS_FR_NOT_ENABLED,			/* (12) The volume has no work area */
	SD_FATFS_FR_NO_FILESYSTEM,		/* (13) There is no valid FAT volume */
	SD_FATFS_FR_MKFS_ABORTED,		/* (14) The f_mkfs() aborted due to any problem */
	SD_FATFS_FR_TIMEOUT,				/* (15) Could not get a grant to access the volume within defined period */
	SD_FATFS_FR_LOCKED,				/* (16) The operation is rejected according to the file sharing policy */
	SD_FATFS_FR_NOT_ENOUGH_CORE,		/* (17) LFN working buffer could not be allocated */
	SD_FATFS_FR_TOO_MANY_OPEN_FILES,	/* (18) Number of open files > FF_FS_LOCK */
	SD_FATFS_FR_INVALID_PARAMETER	/* (19) Given parameter is invalid */
} SD_FATFS_FRESULT;



/*--------------------------------------------------------------*/
/* FatFs module application interface                           */

SD_FATFS_FRESULT sd_fatfs_f_open (SD_FATFS_FIL* fp, const SD_FATFS_TCHAR* path, SD_FATFS_BYTE mode);				/* Open or create a file */
SD_FATFS_FRESULT sd_fatfs_f_close (SD_FATFS_FIL* fp);											/* Close an open file object */
SD_FATFS_FRESULT sd_fatfs_f_read (SD_FATFS_FIL* fp, void* buff, SD_FATFS_UINT btr, SD_FATFS_UINT* br);			/* Read data from the file */
SD_FATFS_FRESULT sd_fatfs_f_write (SD_FATFS_FIL* fp, const void* buff, SD_FATFS_UINT btw, SD_FATFS_UINT* bw);	/* Write data to the file */
SD_FATFS_FRESULT sd_fatfs_f_lseek (SD_FATFS_FIL* fp, SD_FATFS_FSIZE_t ofs);								/* Move file pointer of the file object */
SD_FATFS_FRESULT sd_fatfs_f_truncate (SD_FATFS_FIL* fp);										/* Truncate the file */
SD_FATFS_FRESULT sd_fatfs_f_sync (SD_FATFS_FIL* fp);											/* Flush cached data of the writing file */
SD_FATFS_FRESULT sd_fatfs_f_opendir (SD_FATFS_FF_DIR* dp, const SD_FATFS_TCHAR* path);						/* Open a directory */
SD_FATFS_FRESULT sd_fatfs_f_closedir (SD_FATFS_FF_DIR* dp);										/* Close an open directory */
SD_FATFS_FRESULT sd_fatfs_f_readdir (SD_FATFS_FF_DIR* dp, SD_FATFS_FILINFO* fno);							/* Read a directory item */
SD_FATFS_FRESULT sd_fatfs_f_findfirst (SD_FATFS_FF_DIR* dp, SD_FATFS_FILINFO* fno, const SD_FATFS_TCHAR* path, const SD_FATFS_TCHAR* pattern);	/* Find first file */
SD_FATFS_FRESULT sd_fatfs_f_findnext (SD_FATFS_FF_DIR* dp, SD_FATFS_FILINFO* fno);							/* Find next file */
SD_FATFS_FRESULT sd_fatfs_f_mkdir (const SD_FATFS_TCHAR* path);								/* Create a sub directory */
SD_FATFS_FRESULT sd_fatfs_f_unlink (const SD_FATFS_TCHAR* path);								/* Delete an existing file or directory */
SD_FATFS_FRESULT sd_fatfs_f_rename (const SD_FATFS_TCHAR* path_old, const SD_FATFS_TCHAR* path_new);	/* Rename/Move a file or directory */
SD_FATFS_FRESULT sd_fatfs_f_stat (const SD_FATFS_TCHAR* path, SD_FATFS_FILINFO* fno);					/* Get file status */
SD_FATFS_FRESULT sd_fatfs_f_chmod (const SD_FATFS_TCHAR* path, SD_FATFS_BYTE attr, SD_FATFS_BYTE mask);			/* Change attribute of a file/dir */
SD_FATFS_FRESULT sd_fatfs_f_utime (const SD_FATFS_TCHAR* path, const SD_FATFS_FILINFO* fno);			/* Change timestamp of a file/dir */
SD_FATFS_FRESULT sd_fatfs_f_chdir (const SD_FATFS_TCHAR* path);								/* Change current directory */
SD_FATFS_FRESULT sd_fatfs_f_chdrive (const SD_FATFS_TCHAR* path);								/* Change current drive */
SD_FATFS_FRESULT sd_fatfs_f_getcwd (SD_FATFS_TCHAR* buff, SD_FATFS_UINT len);							/* Get current directory */
SD_FATFS_FRESULT sd_fatfs_f_getfree (const SD_FATFS_TCHAR* path, SD_FATFS_DWORD* nclst, SD_FATFS_FATFS** fatfs);	/* Get number of free clusters on the drive */
SD_FATFS_FRESULT sd_fatfs_f_getlabel (const SD_FATFS_TCHAR* path, SD_FATFS_TCHAR* label, SD_FATFS_DWORD* vsn);	/* Get volume label */
SD_FATFS_FRESULT sd_fatfs_f_setlabel (const SD_FATFS_TCHAR* label);							/* Set volume label */
SD_FATFS_FRESULT sd_fatfs_f_forward (SD_FATFS_FIL* fp, SD_FATFS_UINT(*func)(const SD_FATFS_BYTE*,SD_FATFS_UINT), SD_FATFS_UINT btf, SD_FATFS_UINT* bf);	/* Forward data to the stream */
SD_FATFS_FRESULT sd_fatfs_f_expand (SD_FATFS_FIL* fp, SD_FATFS_FSIZE_t fsz, SD_FATFS_BYTE opt);					/* Allocate a contiguous block to the file */
SD_FATFS_FRESULT sd_fatfs_f_mount (SD_FATFS_FATFS* fs, const SD_FATFS_TCHAR* path, SD_FATFS_BYTE opt);			/* Mount/Unmount a logical drive */
SD_FATFS_FRESULT sd_fatfs_f_mkfs (const SD_FATFS_TCHAR* path, SD_FATFS_BYTE opt, SD_FATFS_DWORD au, void* work, SD_FATFS_UINT len);	/* Create a FAT volume */
SD_FATFS_FRESULT sd_fatfs_f_fdisk (SD_FATFS_BYTE pdrv, const SD_FATFS_DWORD* szt, void* work);			/* Divide a physical drive into some partitions */
SD_FATFS_FRESULT sd_fatfs_f_setcp (SD_FATFS_WORD cp);											/* Set current code page */
int sd_fatfs_f_putc (SD_FATFS_TCHAR c, SD_FATFS_FIL* fp);										/* Put a character to the file */
int sd_fatfs_f_puts (const SD_FATFS_TCHAR* str, SD_FATFS_FIL* cp);								/* Put a string to the file */
int sd_fatfs_f_printf (SD_FATFS_FIL* fp, const SD_FATFS_TCHAR* str, ...);						/* Put a formatted string to the file */
SD_FATFS_TCHAR* sd_fatfs_f_gets (SD_FATFS_TCHAR* buff, int len, SD_FATFS_FIL* fp);						/* Get a string from the file */

#define sd_fatfs_f_eof(fp) ((int)((fp)->fptr == (fp)->obj.objsize))
#define sd_fatfs_f_error(fp) ((fp)->err)
#define sd_fatfs_f_tell(fp) ((fp)->fptr)
#define sd_fatfs_f_size(fp) ((fp)->obj.objsize)
#define sd_fatfs_f_rewind(fp) sd_fatfs_f_lseek((fp), 0)
#define sd_fatfs_f_rewinddir(dp) sd_fatfs_f_readdir((dp), 0)
#define sd_fatfs_f_rmdir(path) sd_fatfs_f_unlink(path)
#define sd_fatfs_f_unmount(path) sd_fatfs_f_mount(0, path, 0)

#ifndef SD_FATFS_EOF
#define SD_FATFS_EOF (-1)
#endif




/*--------------------------------------------------------------*/
/* Additional user defined functions                            */

/* RTC function */
#if !SD_FATFS_FF_FS_READONLY && !SD_FATFS_FF_FS_NORTC
SD_FATFS_DWORD sd_fatfs_get_fattime (void);
#endif

/* LFN support functions */
#if SD_FATFS_FF_USE_LFN >= 1						/* Code conversion (defined in unicode.c) */
SD_FATFS_WCHAR sd_fatfs_ff_oem2uni (SD_FATFS_WCHAR oem, SD_FATFS_WORD cp);	/* OEM code to Unicode conversion */
SD_FATFS_WCHAR sd_fatfs_ff_uni2oem (SD_FATFS_DWORD uni, SD_FATFS_WORD cp);	/* Unicode to OEM code conversion */
SD_FATFS_DWORD sd_fatfs_ff_wtoupper (SD_FATFS_DWORD uni);			/* Unicode upper-case conversion */
#endif
#if SD_FATFS_FF_USE_LFN == 3						/* Dynamic memory allocation */
void* sd_fatfs_ff_memalloc (SD_FATFS_UINT msize);			/* Allocate memory block */
void sd_fatfs_ff_memfree (void* mblock);			/* Free memory block */
#endif

/* Sync functions */
#if SD_FATFS_FF_FS_REENTRANT
int sd_fatfs_ff_cre_syncobj (SD_FATFS_BYTE vol, SD_FATFS_FF_SYNC_t* sobj);	/* Create a sync object */
int sd_fatfs_ff_req_grant (SD_FATFS_FF_SYNC_t sobj);		/* Lock sync object */
void sd_fatfs_ff_rel_grant (SD_FATFS_FF_SYNC_t sobj);		/* Unlock sync object */
int sd_fatfs_ff_del_syncobj (SD_FATFS_FF_SYNC_t sobj);	/* Delete a sync object */
#endif




/*--------------------------------------------------------------*/
/* Flags and offset address                                     */


/* File access mode and open method flags (3rd argument of f_open) */
#define	SD_FATFS_FA_READ				0x01
#define	SD_FATFS_FA_WRITE			0x02
#define	SD_FATFS_FA_OPEN_EXISTING	0x00
#define	SD_FATFS_FA_CREATE_NEW		0x04
#define	SD_FATFS_FA_CREATE_ALWAYS	0x08
#define	SD_FATFS_FA_OPEN_ALWAYS		0x10
#define	SD_FATFS_FA_OPEN_APPEND		0x30

/* Fast seek controls (2nd argument of f_lseek) */
#define SD_FATFS_CREATE_LINKMAP	((SD_FATFS_FSIZE_t)0 - 1)

/* Format options (2nd argument of f_mkfs) */
#define SD_FATFS_FM_FAT		0x01
#define SD_FATFS_FM_FAT32	0x02
#define SD_FATFS_FM_EXFAT	0x04
#define SD_FATFS_FM_ANY		0x07
#define SD_FATFS_FM_SFD		0x08

/* Filesystem type (FATFS.fs_type) */
#define SD_FATFS_FS_FAT12	1
#define SD_FATFS_FS_FAT16	2
#define SD_FATFS_FS_FAT32	3
#define SD_FATFS_FS_EXFAT	4

/* File attribute bits for directory entry (FILINFO.fattrib) */
#define	SD_FATFS_AM_RDO	0x01	/* Read only */
#define	SD_FATFS_AM_HID	0x02	/* Hidden */
#define	SD_FATFS_AM_SYS	0x04	/* System */
#define SD_FATFS_AM_DIR	0x10	/* Directory */
#define SD_FATFS_AM_ARC	0x20	/* Archive */


#ifdef __cplusplus
}
#endif

#endif /* FF_DEFINED */

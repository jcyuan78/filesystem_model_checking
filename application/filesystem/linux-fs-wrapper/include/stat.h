///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

/* SPDX-License-Identifier: GPL-2.0 */

#include "linux_comm.h"

//#include <asm/stat.h>
//#include <uapi/linux/stat.h>

#define S_IRWXUGO	(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
#define S_IRUGO		(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO		(S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO		(S_IXUSR|S_IXGRP|S_IXOTH)

#define UTIME_NOW	((1l << 30) - 1l)
#define UTIME_OMIT	((1l << 30) - 2l)

//#include <linux/types.h>
//#include <linux/time.h>
//#include <linux/uidgid.h>

/*
 * Flags to be stx_mask
 *
 * Query request/result mask for statx() and struct statx::stx_mask.
 *
 * These bits should be set in the mask argument of statx() to request
 * particular items when calling statx().
 */
#define STATX_TYPE		0x00000001U	/* Want/got stx_mode & S_IFMT */
#define STATX_MODE		0x00000002U	/* Want/got stx_mode & ~S_IFMT */
#define STATX_NLINK		0x00000004U	/* Want/got stx_nlink */
#define STATX_UID		0x00000008U	/* Want/got stx_uid */
#define STATX_GID		0x00000010U	/* Want/got stx_gid */
#define STATX_ATIME		0x00000020U	/* Want/got stx_atime */
#define STATX_MTIME		0x00000040U	/* Want/got stx_mtime */
#define STATX_CTIME		0x00000080U	/* Want/got stx_ctime */
#define STATX_INO		0x00000100U	/* Want/got stx_ino */
#define STATX_SIZE		0x00000200U	/* Want/got stx_size */
#define STATX_BLOCKS		0x00000400U	/* Want/got stx_blocks */
#define STATX_BASIC_STATS	0x000007ffU	/* The stuff in the normal stat struct */
#define STATX_BTIME		0x00000800U	/* Want/got stx_btime */
#define STATX_MNT_ID		0x00001000U	/* Got stx_mnt_id */

#define STATX__RESERVED		0x80000000U	/* Reserved for future struct statx expansion */

#ifndef __KERNEL__
 /*
  * This is deprecated, and shall remain the same value in the future.  To avoid
  * confusion please use the equivalent (STATX_BASIC_STATS | STATX_BTIME)
  * instead.
  */
#define STATX_ALL		0x00000fffU
#endif

  /*
   * Attributes to be found in stx_attributes and masked in stx_attributes_mask.
   *
   * These give information about the features or the state of a file that might
   * be of use to ordinary userspace programs such as GUIs or ls rather than
   * specialised tools.
   *
   * Note that the flags marked [I] correspond to the FS_IOC_SETFLAGS flags
   * semantically.  Where possible, the numerical value is picked to correspond
   * also.  Note that the DAX attribute indicates that the file is in the CPU
   * direct access state.  It does not correspond to the per-inode flag that
   * some filesystems support.
   *
   */
#define STATX_ATTR_COMPRESSED		0x00000004 /* [I] File is compressed by the fs */
#define STATX_ATTR_IMMUTABLE		0x00000010 /* [I] File is marked immutable */
#define STATX_ATTR_APPEND		0x00000020 /* [I] File is append-only */
#define STATX_ATTR_NODUMP		0x00000040 /* [I] File is not to be dumped */
#define STATX_ATTR_ENCRYPTED		0x00000800 /* [I] File requires key to decrypt in fs */
#define STATX_ATTR_AUTOMOUNT		0x00001000 /* Dir: Automount trigger */
#define STATX_ATTR_MOUNT_ROOT		0x00002000 /* Root of a mount */
#define STATX_ATTR_VERITY		0x00100000 /* [I] Verity protected file */
#define STATX_ATTR_DAX			0x00200000 /* File is currently in DAX state */



struct kstat 
{
	u32		result_mask;	/* What fields the user got */
	umode_t		mode;
	unsigned int	nlink;
	uint32_t	blksize;	/* Preferred I/O size */
	u64		attributes;
	u64		attributes_mask;
#define KSTAT_ATTR_FS_IOC_FLAGS				\
	(STATX_ATTR_COMPRESSED |			\
	 STATX_ATTR_IMMUTABLE |				\
	 STATX_ATTR_APPEND |				\
	 STATX_ATTR_NODUMP |				\
	 STATX_ATTR_ENCRYPTED |				\
	 STATX_ATTR_VERITY				\
	 )/* Attrs corresponding to FS_*_FL flags */
	u64		ino;
	dev_t		dev;
	dev_t		rdev;
	//kuid_t		uid;
	//kgid_t		gid;
	loff_t		size;
	//timespec64 atime;
	//timespec64 mtime;
	//timespec64 ctime;
	//timespec64 btime;			/* File creation time */
	time64_t atime;
	time64_t mtime;
	time64_t ctime;
	time64_t btime;

	u64		blocks;
	u64		mnt_id;
};


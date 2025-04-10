﻿///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * f2fs_fs.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 * Copyright (c) 2019 Google Inc.
 *             http://www.google.com/
 * Copyright (c) 2020 Google Inc.
 *   Robin Hsu <robinhsu@google.com>
 *  : add sload compression support
 *
 * Dual licensed under the GPL or LGPL version 2 licenses.
 *
 * The byteswap codes are copied from:
 *   samba_3_master/lib/ccan/endian/endian.h under LGPL 2.1
 */
#ifndef __F2FS_FS_H__
#define __F2FS_FS_H__

#define HAVE_CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <linux-fs-wrapper.h>
#include <dokanfs-lib.h>

//#include "f2fs.h"

#ifdef __ANDROID__
#define WITH_ANDROID
#endif

#ifdef WITH_ANDROID
#include <android_config.h>
#else
#define WITH_DUMP
#define WITH_DEFRAG
#define WITH_RESIZE
#define WITH_SLOAD
#define WITH_LABEL
#endif

#include <inttypes.h>
#ifdef HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif
#include <sys/types.h>

#ifdef HAVE_LINUX_BLKZONED_H
#include <linux/blkzoned.h>
#endif

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#include <selinux/label.h>
#endif

#ifdef UNUSED
#elif defined(__GNUC__)
# define UNUSED(x) UNUSED_ ## x __attribute__((unused))
#elif defined(__LCLINT__)
# define UNUSED(x) x
#elif defined(__cplusplus)
# define UNUSED(x)
#else
# define UNUSED(x) x
#endif

#ifdef ANDROID_WINDOWS_HOST
#undef HAVE_LINUX_TYPES_H
typedef uint64_t u_int64_t;
typedef uint32_t u_int32_t;
typedef uint16_t u_int16_t;
typedef uint8_t u_int8_t;
#endif

/* codes from kernel's f2fs.h, GPL-v2.0 */
#define MIN_COMPRESS_LOG_SIZE	2
#define MAX_COMPRESS_LOG_SIZE	8

//typedef u_int64_t	u64;
//typedef u_int32_t	u32;
//typedef u_int16_t	u16;
//typedef u_int8_t	u8;
typedef UINT32		block_t;
typedef UINT32		nid_t;
//#ifndef bool
//typedef u8		bool;
//#endif
//typedef unsigned long	pgoff_t;

#ifndef HAVE_LINUX_TYPES_H
typedef UINT8	__u8;
typedef UINT16	__u16;
typedef UINT32	__u32;
typedef UINT64	__u64;
typedef UINT16	__le16;
typedef UINT32	__le32;
typedef UINT64	__le64;
typedef UINT16	__be16;
typedef UINT32	__be32;
typedef UINT64	__be64;
#endif





/*
 * code borrowed from kernel f2fs dirver: f2fs.h, GPL-2.0
 *  : definitions of COMPRESS_DATA_RESERVED_SIZE,
 *    struct compress_data, COMPRESS_HEADER_SIZE,
 *    and struct compress_ctx
 */
#define COMPRESS_DATA_RESERVED_SIZE		4
struct compress_data 
{
	__le32 clen;			/* compressed data size */
	__le32 chksum;			/* checksum of compressed data */
	__le32 reserved[COMPRESS_DATA_RESERVED_SIZE];	/* reserved */
	UINT8 cdata[1];			/* compressed data */
};
#define COMPRESS_HEADER_SIZE	(sizeof(struct compress_data))
/* compress context */
/* compress context */
struct compress_ctx
{
	struct inode* inode;		/* inode the context belong to */
	pgoff_t cluster_idx;		/* cluster index number */
	unsigned int cluster_size;	/* page count in cluster */
	unsigned int log_cluster_size;	/* log of cluster size */
	struct page** rpages;		/* pages store raw data in cluster */
	unsigned int nr_rpages;		/* total page number in rpages */
	struct page** cpages;		/* pages store compressed data in cluster */
	unsigned int nr_cpages;		/* total page number in cpages */
	void* rbuf;			/* virtual mapped address on rpages */
	struct compress_data* cbuf;	/* virtual mapped address on cpages */
	size_t rlen;			/* valid data length in rbuf */
	size_t clen;			/* valid data length in cbuf */
	void* private1;			/* payload buffer for specified compression algorithm */
	void* private2;			/* extra payload buffer */
};

#if HAVE_BYTESWAP_H
#include <byteswap.h>
#else
/**
 * bswap_16 - reverse bytes in a uint16_t value.
 * @val: value whose bytes to swap.
 *
 * Example:
 *	// Output contains "1024 is 4 as two bytes reversed"
 *	printf("1024 is %u as two bytes reversed\n", bswap_16(1024));
 */
static inline uint16_t bswap_16(uint16_t val)
{
	return ((val & (uint16_t)0x00ffU) << 8)	| ((val & (uint16_t)0xff00U) >> 8);
}

/**
 * bswap_32 - reverse bytes in a uint32_t value.
 * @val: value whose bytes to swap.
 *
 * Example:
 *	// Output contains "1024 is 262144 as four bytes reversed"
 *	printf("1024 is %u as four bytes reversed\n", bswap_32(1024));
 */
static inline uint32_t bswap_32(uint32_t val)
{
	return ((val & (uint32_t)0x000000ffUL) << 24)
		| ((val & (uint32_t)0x0000ff00UL) <<  8)
		| ((val & (uint32_t)0x00ff0000UL) >>  8)
		| ((val & (uint32_t)0xff000000UL) >> 24);
}
#endif /* !HAVE_BYTESWAP_H */

#if defined HAVE_DECL_BSWAP_64 && !HAVE_DECL_BSWAP_64
/**
 * bswap_64 - reverse bytes in a uint64_t value.
 * @val: value whose bytes to swap.
 *
 * Example:
 *	// Output contains "1024 is 1125899906842624 as eight bytes reversed"
 *	printf("1024 is %llu as eight bytes reversed\n",
 *		(unsigned long long)bswap_64(1024));
 */
static inline uint64_t bswap_64(uint64_t val)
{
	return ((val & (uint64_t)0x00000000000000ffULL) << 56)
		| ((val & (uint64_t)0x000000000000ff00ULL) << 40)
		| ((val & (uint64_t)0x0000000000ff0000ULL) << 24)
		| ((val & (uint64_t)0x00000000ff000000ULL) <<  8)
		| ((val & (uint64_t)0x000000ff00000000ULL) >>  8)
		| ((val & (uint64_t)0x0000ff0000000000ULL) >> 24)
		| ((val & (uint64_t)0x00ff000000000000ULL) >> 40)
		| ((val & (uint64_t)0xff00000000000000ULL) >> 56);
}
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define le16_to_cpu(x)	((__u16)(x))
#define le32_to_cpu(x)	((__u32)(x))
#define le64_to_cpu(x)	((__u64)(x))
#define cpu_to_le16(x)	((__u16)(x))
#define cpu_to_le32(x)	((__u32)(x))
#define cpu_to_le64(x)	((__u64)(x))
#elif __BYTE_ORDER == __BIG_ENDIAN
#define le16_to_cpu(x)	bswap_16(x)
#define le32_to_cpu(x)	bswap_32(x)
#define le64_to_cpu(x)	bswap_64(x)
#define cpu_to_le16(x)	bswap_16(x)
#define cpu_to_le32(x)	bswap_32(x)
#define cpu_to_le64(x)	bswap_64(x)
#endif

#define typecheck(type,x) \
	({	type __dummy; \
		typeof(x) __dummy2; \
		(void)(&__dummy == &__dummy2); \
		1; \
	 })

//#define NULL_SEGNO	((unsigned int)~0)

/*
 * Debugging interfaces
 */
#define FIX_MSG(fmt, ...)						\
	do {								\
		printf("[FIX] (%s:%4d) ", __func__, __LINE__);		\
		printf(" --> " fmt "\n", ##__VA_ARGS__);			\
	} while (0)

#define ASSERT_MSG(fmt, ...)						\
	do {								\
		printf("[ASSERT] (%s:%4d) ", __func__, __LINE__);	\
		printf(" --> " fmt "\n", ##__VA_ARGS__);			\
		c.bug_on = 1;						\
	} while (0)

#define ASSERT(exp)		do {					\
		if (!(exp)) {						\
			printf("[ASSERT] (%s:%4d) %s\n", __func__, __LINE__, #exp);	\
			THROW_ERROR(ERR_APP, L"[ASSERT] (%S:%4d) %S\n", __func__, __LINE__, #exp) ;	\
			/*exit(-1);*/ \
		}	} while (0)

#define ERR_MSG(fmt, ...)						\
	do {								\
		printf("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); \
	} while (0)

#define MSG(n, fmt, ...)						\
	do {								\
		if (c.dbg_lv >= n && !c.layout && !c.show_file_map) {	\
			printf(fmt, ##__VA_ARGS__);			\
		}							\
	} while (0)
//#define MSG(n, fmt, ...)		LOG_NOTICE(L##fmt , __VA_ARGS__)

#define DBG(n, fmt, ...)						\
	do {								\
		if (c.dbg_lv >= n && !c.layout && !c.show_file_map) {	\
			printf("[%s:%4d] " fmt,				\
				__func__, __LINE__, ##__VA_ARGS__);	\
		}							\
	} while (0)

/* Display on console */
#define DISP(fmt, ptr, member)				\
	do {						\
		printf("%-30s" fmt, #member, ((ptr)->member));	\
	} while (0)

#define DISP_u16(ptr, member)						\
	do {								\
		assert(sizeof((ptr)->member) == 2);			\
		if (c.layout)						\
			printf("%-30s %u\n",				\
			#member":", le16_to_cpu(((ptr)->member)));	\
		else							\
			printf("%-30s" "\t\t[0x%8x : %u]\n",		\
			#member, le16_to_cpu(((ptr)->member)),		\
			le16_to_cpu(((ptr)->member)));			\
	} while (0)

#define DISP_u32(ptr, member)						\
	do {								\
		assert(sizeof((ptr)->member) <= 4);			\
		if (c.layout)						\
			printf("%-30s %u\n",				\
			#member":", le32_to_cpu(((ptr)->member)));	\
		else							\
			printf("%-30s" "\t\t[0x%8x : %u]\n",		\
			#member, le32_to_cpu(((ptr)->member)),		\
			le32_to_cpu(((ptr)->member)));			\
	} while (0)

#define DISP_u64(ptr, member)						\
	do {								\
		assert(sizeof((ptr)->member) == 8);			\
		if (c.layout)						\
			printf("%-30s %llu\n",				\
			#member":", le64_to_cpu(((ptr)->member)));	\
		else							\
			printf("%-30s" "\t\t[0x%8llx : %llu]\n",	\
			#member, le64_to_cpu(((ptr)->member)),		\
			le64_to_cpu(((ptr)->member)));			\
	} while (0)

#define DISP_utf(ptr, member)						\
	do {								\
		if (c.layout)						\
			printf("%-30s %s\n", #member":",		\
					((ptr)->member)); 		\
		else							\
			printf("%-30s" "\t\t[%s]\n", #member,		\
					((ptr)->member));		\
	} while (0)

/* Display to buffer */
#define BUF_DISP_u32(buf, data, len, ptr, member)			\
	do {								\
		assert(sizeof((ptr)->member) <= 4);			\
		snprintf(buf, len, #member);				\
		snprintf(data, len, "0x%x : %u", ((ptr)->member),	\
						((ptr)->member));	\
	} while (0)

#define BUF_DISP_u64(buf, data, len, ptr, member)			\
	do {								\
		assert(sizeof((ptr)->member) == 8);			\
		snprintf(buf, len, #member);				\
		snprintf(data, len, "0x%llx : %llu", ((ptr)->member),	\
						((ptr)->member));	\
	} while (0)

#define BUF_DISP_utf(buf, data, len, ptr, member)			\
		snprintf(buf, len, #member)

/* these are defined in kernel */
#ifndef PAGE_SIZE
#define PAGE_SIZE		4096
#endif
#define PAGE_CACHE_SIZE		4096
//#define BITS_PER_BYTE		8
#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT		9
#endif
#define F2FS_SUPER_MAGIC	0xF2F52010	/* F2FS Magic Number */
#define CP_CHKSUM_OFFSET	4092
#define SB_CHKSUM_OFFSET	3068
#define MAX_PATH_LEN		64
#define MAX_DEVICES		8

#define F2FS_BYTES_TO_BLK(bytes)    ((bytes) >> F2FS_BLKSIZE_BITS)
#define F2FS_BLKSIZE_BITS 12

/* for mkfs */
#define	F2FS_NUMBER_OF_CHECKPOINT_PACK	2
#define	DEFAULT_SECTOR_SIZE		512
#define	DEFAULT_SECTORS_PER_BLOCK	8
#define	DEFAULT_BLOCKS_PER_SEGMENT	512
#define DEFAULT_SEGMENTS_PER_SECTION	1

#define VERSION_LEN	256

#define LPF "lost+found"

enum f2fs_config_func {
	MKFS,
	FSCK,
	DUMP,
	DEFRAG,
	RESIZE,
	SLOAD,
	LABEL,
};

enum default_set {
	CONF_NONE = 0,
	CONF_ANDROID,
};

struct device_info {
//	char *path;
//	int32_t fd;
//	FILE* fd;
	IVirtualDisk* m_fd;
	UINT32 sector_size;
	UINT64 total_sectors;	/* got by get_device_info */
	UINT64 start_blkaddr;
	UINT64 end_blkaddr;
	UINT32 total_segments;

	/* to handle zone block devices */
	int zoned_model;
	UINT32 nr_zones;
	UINT32 nr_rnd_zones;
	size_t zone_blocks;
	size_t *zone_cap_blocks;
};

typedef struct {
	/* Value 0 means no cache, minimum 1024 */
	long num_cache_entry;

	/* Value 0 means always overwrite (no collision allowed). maximum 16 */
	unsigned max_hash_collision;

	bool dbg_en;
} dev_cache_config_t;

/* f2fs_configration for compression used for sload.f2fs */
typedef struct  {
	void (*init)(struct compress_ctx *cc);
	int (*compress)(struct compress_ctx *cc);
	void (*reset)(struct compress_ctx *cc);
} compress_ops;

/* Should be aligned to supported_comp_names and support_comp_ops */
enum compress_algorithms {
	COMPR_LZO,
	COMPR_LZ4,
	MAX_COMPRESS_ALGS,
};

enum filter_policy {
	COMPR_FILTER_UNASSIGNED = 0,
	COMPR_FILTER_ALLOW,
	COMPR_FILTER_DENY,
};

typedef struct {
	void (*add)(const char *);
	void (*destroy)(void);
	bool (*filter)(const char *);
} filter_ops;

typedef struct {
	bool enabled;			/* disabled by default */
	bool required;			/* require to enable */
	bool readonly;			/* readonly to release blocks */
	struct compress_ctx cc;		/* work context */
	enum compress_algorithms alg;	/* algorithm to compress */
	compress_ops *ops;		/* ops per algorithm */
	unsigned int min_blocks;	/* save more blocks than this */
	enum filter_policy filter;	/* filter to try compression */
	filter_ops *filter_ops;		/* filter ops */
} compress_config_t;

#define ALIGN_UP(value, size) ((value) + ((value) % (size) > 0 ? (size) - (value) % (size) : 0))

struct f2fs_configuration {
	UINT32 reserved_segments;
	UINT32 new_reserved_segments;
	int sparse_mode;
	int zoned_mode;
	int zoned_model;
	size_t zone_blocks;
	double overprovision;
	double new_overprovision;
	UINT32 cur_seg[6];
	UINT32 segs_per_sec;
	UINT32 secs_per_zone;
	UINT32 segs_per_zone;
	UINT32 start_sector;
	UINT32 total_segments;
	UINT32 sector_size;
	UINT64 device_size;
	UINT64 total_sectors;
	UINT64 wanted_total_sectors;
	UINT64 wanted_sector_size;
	UINT64 target_sectors;
	UINT64 max_size;
	UINT32 sectors_per_blk;
	UINT32 blks_per_seg;
	__u8 init_version[VERSION_LEN + 1];
	__u8 sb_version[VERSION_LEN + 1];
	char version[VERSION_LEN + 1];
//	char *vol_label;
//	std::wstring m_vol_label;
	char *vol_uuid;
	UINT16 s_encoding;
	UINT16 s_encoding_flags;
	int heap;
	int32_t kd;				// file of process version
	IVirtualDisk* m_kd;
//	int32_t dump_fd;
	IVirtualDisk* m_dump_fd;
	struct device_info devices[MAX_DEVICES];
	int ndevs;
	char *extension_list[2];
	const char *rootdev_name;
	int dbg_lv;
	int show_dentry;
	int trim;
	int trimmed;
	int func;
	void *private_data;
	int dry_run;
	int no_kernel_check;
	int fix_on;
	int force;
	int defset;
	int bug_on;
	int bug_nat_bits;
	int alloc_failed;
	int auto_fix;
	int layout;
	int show_file_map;
	UINT64 show_file_map_max_offset;
	int quota_fix;
	int preen_mode;
	int ro;
	int preserve_limits;		/* preserve quota limits */
	int large_nat_bitmap;
	int fix_chksum;			/* fix old cp.chksum position */
	__le32 feature;			/* defined features */
	time_t fixed_time;

	/* mkfs parameters */
	int fake_seed;
	UINT32 next_free_nid;
	UINT32 quota_inum;
	UINT32 quota_dnum;
	UINT32 lpf_inum;
	UINT32 lpf_dnum;
	UINT32 lpf_ino;
	UINT32 root_uid;
	UINT32 root_gid;

	/* defragmentation parameters */
	int defrag_shrink;
	UINT64 defrag_start;
	UINT64 defrag_len;
	UINT64 defrag_target;

	/* sload parameters */
	char *from_dir;
	char *mount_point;
	char *target_out_dir;
	char *fs_config_file;
#ifdef HAVE_LIBSELINUX
	struct selinux_opt seopt_file[8];
	int nr_opt;
#endif
	int preserve_perms;

	/* resize parameters */
	int safe_resize;

	/* precomputed fs UUID checksum for seeding other checksums */
	UINT32 chksum_seed;

	/* cache parameters */
	dev_cache_config_t cache_config;

	/* compression support for sload.f2fs */
	compress_config_t compress;

// for debug
	bool dump_index_nodes;
	bool dump_segments;
};



//#define BIT_MASK(nr)	(1 << (nr % BITS_PER_LONG))
//#define BIT_WORD(nr)	(nr / BITS_PER_LONG)

#define set_sb_le64(member, val)		(sb->member = cpu_to_le64(val))
#define set_sb_le32(member, val)		(sb->member = cpu_to_le32(val))
#define set_sb_le16(member, val)		(sb->member = cpu_to_le16(val))
#define get_sb_le64(member)			le64_to_cpu(sb->member)
#define get_sb_le32(member)			le32_to_cpu(sb->member)
#define get_sb_le16(member)			le16_to_cpu(sb->member)
#define get_newsb_le64(member)			le64_to_cpu(new_sb->member)
#define get_newsb_le32(member)			le32_to_cpu(new_sb->member)
#define get_newsb_le16(member)			le16_to_cpu(new_sb->member)

#define set_sb(member, val)	\
			do {						\
				/*typeof(sb->member) t;*/			\
				switch (sizeof(sb->member)) {			\
				case 8: set_sb_le64(member, val); break; \
				case 4: set_sb_le32(member, val); break; \
				case 2: set_sb_le16(member, val); break; \
				} \
			} while(0)

//template <typename T1, typename T2>
//inline void set_sb(T1& member, T2 val)
//{
//	case 8: set_sb_le64(member, val); break; \
//	case 4: set_sb_le32(member, val); break; \
//	case 2: set_sb_le16(member, val); break; \
//}

/*
#define get_sb(member)		\
			({						\
				typeof(sb->member) t;			\
				switch (sizeof(sb->member)) {			\
				case 8: t = get_sb_le64(member); break; \
				case 4: t = get_sb_le32(member); break; \
				case 2: t = get_sb_le16(member); break; \
				} 					\
				t; \
		})	*/

#define get_sb(member) (sb->member)

#define get_newsb(member)		\
			({						\
				typeof(new_sb->member) t;		\
				switch (sizeof(t)) {			\
				case 8: t = get_newsb_le64(member); break; \
				case 4: t = get_newsb_le32(member); break; \
				case 2: t = get_newsb_le16(member); break; \
				} 					\
				t; \
			})

#define set_cp_le64(member, val)		(cp->member = cpu_to_le64(val))
#define set_cp_le32(member, val)		(cp->member = cpu_to_le32(val))
#define set_cp_le16(member, val)		(cp->member = cpu_to_le16(val))
#define get_cp_le64(member)			le64_to_cpu(cp->member)
#define get_cp_le32(member)			le32_to_cpu(cp->member)
#define get_cp_le16(member)			le16_to_cpu(cp->member)

#define set_cp(member, val)	\
			do {						\
				/*typeof(cp->member) t;*/			\
				switch (sizeof(cp->member)) {			\
				case 8: set_cp_le64(member, val); break; \
				case 4: set_cp_le32(member, val); break; \
				case 2: set_cp_le16(member, val); break; \
				} \
			} while(0)
/*
#define get_cp(member)		\
			({						\
				typeof(cp->member) t;			\
				switch (sizeof(t)) {			\
				case 8: t = get_cp_le64(member); break; \
				case 4: t = get_cp_le32(member); break; \
				case 2: t = get_cp_le16(member); break; \
				} 					\
				t; \
			})
*/

template <typename T> T get_cp_le(T val);
template<> inline UINT64 get_cp_le(UINT64 val) { return le64_to_cpu(val); }
template<> inline UINT32 get_cp_le(UINT32 val) { return le32_to_cpu(val); }
template<> inline UINT16 get_cp_le(UINT16 val) { return le16_to_cpu(val); }

#define get_cp(member)	get_cp_le(cp->member)

/*
 * Copied from fs/f2fs/f2fs.h
 */
#define	NR_CURSEG_DATA_TYPE	(3)
#define NR_CURSEG_NODE_TYPE	(3)
//#define NR_CURSEG_TYPE	(NR_CURSEG_DATA_TYPE + NR_CURSEG_NODE_TYPE)

enum SEGMENT_TYPE
{
	CURSEG_HOT_DATA = 0,							/* directory entry blocks */
	CURSEG_WARM_DATA,								/* data blocks */
	CURSEG_COLD_DATA,								/* multimedia or GCed data blocks */
	CURSEG_HOT_NODE,								/* direct node blocks of directory files */
	CURSEG_WARM_NODE,								/* direct node blocks of normal files */
	CURSEG_COLD_NODE,								/* indirect node blocks */
	NR_PERSISTENT_LOG,								/* number of persistent log */
	CURSEG_COLD_DATA_PINNED = NR_PERSISTENT_LOG,	/* pinned file that needs consecutive block address */
	CURSEG_ALL_DATA_ATGC,							/* SSR alloctor in hot/warm/cold data area */
	NO_CHECK_TYPE,									/* number of persistent & inmem log */
};

#define F2FS_MIN_SEGMENTS	9 /* SB + 2 (CP + SIT + NAT) + SSA + MAIN */

/*
 * Copied from fs/f2fs/segment.h
 */
#define GET_SUM_TYPE(footer) ((footer)->entry_type)
//#define SET_SUM_TYPE(footer, type) ((footer)->entry_type = type)

/*
 * Copied from include/linux/f2fs_sb.h
 */
#define F2FS_SUPER_OFFSET		1024	/* byte-size offset */
#define F2FS_MIN_LOG_SECTOR_SIZE	9	/* 9 bits for 512 bytes */
#define F2FS_MAX_LOG_SECTOR_SIZE	12	/* 12 bits for 4096 bytes */
#define F2FS_LOG_SECTORS_PER_BLOCK	3	/* log number for sector/blk */
#define F2FS_BLKSIZE			4096	/* support only 4KB block */
#define F2FS_BLKSIZE_BITS		12	/* bits for F2FS_BLKSIZE */
#define F2FS_MAX_EXTENSION		64	/* # of extension entries */
#define F2FS_EXTENSION_LEN		8	/* max size of extension */
#define F2FS_BLK_ALIGN(x)	(((x) + F2FS_BLKSIZE - 1) / F2FS_BLKSIZE)

#define NULL_ADDR		0x0U
#define NEW_ADDR		(0-1U)
#define COMPRESS_ADDR		(0-2U)

#define F2FS_BYTES_TO_BLK(bytes)	((bytes) >> F2FS_BLKSIZE_BITS)
#define F2FS_BLK_TO_BYTES(blk)		((blk) << F2FS_BLKSIZE_BITS)

/* 0, 1(node nid), 2(meta nid) are reserved node id */
#define F2FS_RESERVED_NODE_NUM		3
//#define F2FS_ROOT_INO(sbi)	(sbi->root_ino_num)
//#define F2FS_NODE_INO(sbi)	(sbi->node_ino_num)
//#define F2FS_META_INO(sbi)	(sbi->meta_ino_num)

#define F2FS_MAX_QUOTAS		3
#define QUOTA_DATA(i)		(2)
#define QUOTA_INO(sb,t)	(le32_to_cpu((sb)->qf_ino[t]))

#define FS_IMMUTABLE_FL		0x00000010 /* Immutable file */

#define F2FS_ENC_UTF8_12_1	1
#define F2FS_IO_SIZE(sbi)	((size_t)1 << F2FS_OPTION(sbi).write_io_size_bits) /* Blocks */
#define F2FS_IO_SIZE_KB(sbi)	(1 << (F2FS_OPTION(sbi).write_io_size_bits + 2)) /* KB */
#define F2FS_IO_SIZE_BYTES(sbi)	(1 << (F2FS_OPTION(sbi).write_io_size_bits + 12)) /* B */
#define F2FS_IO_SIZE_BITS(sbi)	(F2FS_OPTION(sbi).write_io_size_bits) /* power of 2 */
#define F2FS_IO_SIZE_MASK(sbi)	(F2FS_IO_SIZE(sbi) - 1)
#define F2FS_IO_ALIGNED(sbi)	(F2FS_IO_SIZE(sbi) > 1)

#define F2FS_ENC_STRICT_MODE_FL	(1 << 0)

/* This flag is used by node and meta inodes, and by recovery */
#define GFP_F2FS_ZERO	(GFP_NOFS | __GFP_ZERO)

/*
 * For further optimization on multi-head logs, on-disk layout supports maximum
 * 16 logs by default. The number, 16, is expected to cover all the cases
 * enoughly. The implementaion currently uses no more than 6 logs.
 * Half the logs are used for nodes, and the other half are used for data.
 */
#define MAX_ACTIVE_LOGS	16
#define MAX_ACTIVE_NODE_LOGS	8
#define MAX_ACTIVE_DATA_LOGS	8

#define F2FS_FEATURE_ENCRYPT		0x0001
#define F2FS_FEATURE_BLKZONED		0x0002
#define F2FS_FEATURE_ATOMIC_WRITE	0x0004
#define F2FS_FEATURE_EXTRA_ATTR		0x0008
#define F2FS_FEATURE_PRJQUOTA		0x0010
#define F2FS_FEATURE_INODE_CHKSUM	0x0020
#define F2FS_FEATURE_FLEXIBLE_INLINE_XATTR	0x0040
#define F2FS_FEATURE_QUOTA_INO		0x0080
#define F2FS_FEATURE_INODE_CRTIME	0x0100
#define F2FS_FEATURE_LOST_FOUND		0x0200
#define F2FS_FEATURE_VERITY		0x0400	/* reserved */
#define F2FS_FEATURE_SB_CHKSUM		0x0800
#define F2FS_FEATURE_CASEFOLD		0x1000
#define F2FS_FEATURE_COMPRESSION	0x2000
#define F2FS_FEATURE_RO			0x4000

#define VERSION_LEN	256
#define MAX_VOLUME_NAME		512
#define MAX_PATH_LEN		64
#define MAX_DEVICES		8

/*
 * For superblock
 */
#pragma pack(push, 1)
struct f2fs_device 
{	//<TODO> 确认一下f2fs_device的用途
	__u8 path[MAX_PATH_LEN];	// 考虑到相容性问题，确保这个空间有效
	__le32 total_segments;		// 原设计中，利用path判断f2fs_device是否有效，修改为0xFFFFFFFF表示无效。
} ;

struct f2fs_super_block {
	__le32 magic;			/* Magic Number */
	__le16 major_ver;		/* Major Version */
	__le16 minor_ver;		/* Minor Version */
	__le32 log_sectorsize;		/* log2 sector size in bytes */
	__le32 log_sectors_per_block;	/* log2 # of sectors per block */
	__le32 log_blocksize;		/* log2 block size in bytes */
	__le32 log_blocks_per_seg;	/* log2 # of blocks per segment */
	__le32 segs_per_sec;		/* # of segments per section */
	__le32 secs_per_zone;		/* # of sections per zone */
	__le32 checksum_offset;		/* checksum offset inside super block */
	__le64 block_count;		/* total # of user blocks */
	__le32 section_count;		/* total # of sections */
	__le32 segment_count;		/* total # of segments */
	__le32 segment_count_ckpt;	/* # of segments for checkpoint */
	__le32 segment_count_sit;	/* # of segments for SIT */
	__le32 segment_count_nat;	/* # of segments for NAT */
	__le32 segment_count_ssa;	/* # of segments for SSA */
	__le32 segment_count_main;	/* # of segments for main area */
	__le32 segment0_blkaddr;	/* start block address of segment 0 */
	__le32 cp_blkaddr;		/* start block address of checkpoint */
	__le32 sit_blkaddr;		/* start block address of SIT */
	__le32 nat_blkaddr;		/* start block address of NAT */
	__le32 ssa_blkaddr;		/* start block address of SSA */
	__le32 main_blkaddr;		/* start block address of main area */
	__le32 root_ino;		/* root inode number */
	__le32 node_ino;		/* node inode number */
	__le32 meta_ino;		/* meta inode number */
	__u8 uuid[16];			/* 128-bit uuid for volume */
	wchar_t volume_name[MAX_VOLUME_NAME];	/* volume name */
	__le32 extension_count;		/* # of extensions below */
	__u8 extension_list[F2FS_MAX_EXTENSION][8];	/* extension array */
	__le32 cp_payload;
	__u8 version[VERSION_LEN];	/* the kernel version */
	__u8 init_version[VERSION_LEN];	/* the initial kernel version */
	__le32 feature;			/* defined features */
	__u8 encryption_level;		/* versioning level for encryption */
	__u8 encrypt_pw_salt[16];	/* Salt used for string2key algorithm */
	struct f2fs_device devs[MAX_DEVICES];	/* device list */
	__le32 qf_ino[F2FS_MAX_QUOTAS];	/* quota inode numbers */
	__u8 hot_ext_count;		/* # of hot file extension */
	__le16  s_encoding;		/* Filename charset encoding */
	__le16  s_encoding_flags;	/* Filename charset encoding flags */
	__u8 reserved[306];		/* valid reserved region */
	__le32 crc;			/* checksum of superblock */
} ;

/*
 * For checkpoint
 */
#define CP_RESIZEFS_FLAG                0x00004000
#define CP_DISABLED_QUICK_FLAG		0x00002000
#define CP_DISABLED_FLAG		0x00001000
#define CP_QUOTA_NEED_FSCK_FLAG		0x00000800
#define CP_LARGE_NAT_BITMAP_FLAG	0x00000400
#define CP_NOCRC_RECOVERY_FLAG	0x00000200
#define CP_TRIMMED_FLAG		0x00000100
#define CP_NAT_BITS_FLAG	0x00000080
#define CP_CRC_RECOVERY_FLAG	0x00000040
#define CP_FASTBOOT_FLAG	0x00000020
#define CP_FSCK_FLAG		0x00000010
#define CP_ERROR_FLAG		0x00000008
#define CP_COMPACT_SUM_FLAG	0x00000004
#define CP_ORPHAN_PRESENT_FLAG	0x00000002
#define CP_UMOUNT_FLAG		0x00000001

#define F2FS_CP_PACKS		2	/* # of checkpoint packs */

//<YUAN> raw data
struct f2fs_checkpoint 
{
	__le64 checkpoint_ver;		/* checkpoint block version number */
	__le64 user_block_count;	/* # of user blocks */
	__le64 valid_block_count;	/* # of valid blocks in main area */
	__le32 rsvd_segment_count;	/* # of reserved segments for gc */
	__le32 overprov_segment_count;	/* # of overprovision segments */
	__le32 free_segment_count;	/* # of free segments in main area */

	/* information of current node segments */
	__le32 cur_node_segno[MAX_ACTIVE_NODE_LOGS];
	__le16 cur_node_blkoff[MAX_ACTIVE_NODE_LOGS];
	/* information of current data segments */
	__le32 cur_data_segno[MAX_ACTIVE_DATA_LOGS];
	__le16 cur_data_blkoff[MAX_ACTIVE_DATA_LOGS];
	__le32 ckpt_flags;		/* Flags : umount and journal_present */
	__le32 cp_pack_total_block_count;	/* total # of one cp pack */
	__le32 cp_pack_start_sum;	/* start block number of data summary */
	__le32 valid_node_count;	/* Total number of valid nodes */
	__le32 valid_inode_count;	/* Total number of valid inodes */
	__le32 next_free_nid;		/* Next free node number */
	__le32 sit_ver_bitmap_bytesize;	/* Default value 64 */
	__le32 nat_ver_bitmap_bytesize; /* Default value 256 */
	__le32 checksum_offset;		/* checksum offset inside cp block */
	__le64 elapsed_time;		/* mounted time */
	/* allocation type of current segment */
	unsigned char alloc_type[MAX_ACTIVE_LOGS];

	/* SIT and NAT version bitmap */
	unsigned char sit_nat_version_bitmap[1];
} ;

#define CP_CHKSUM_OFFSET	4092	/* default chksum offset in checkpoint */
//#define CP_MIN_CHKSUM_OFFSET						\
//	(offsetof(struct f2fs_checkpoint, sit_nat_version_bitmap))

#define CP_BITMAP_OFFSET		(offsetof(struct f2fs_checkpoint, sit_nat_version_bitmap))
#define CP_MIN_CHKSUM_OFFSET	CP_BITMAP_OFFSET

#define MIN_NAT_BITMAP_SIZE				64
#define MAX_SIT_BITMAP_SIZE_IN_CKPT		(CP_CHKSUM_OFFSET - CP_BITMAP_OFFSET - MIN_NAT_BITMAP_SIZE)
#define MAX_BITMAP_SIZE_IN_CKPT			(CP_CHKSUM_OFFSET - CP_BITMAP_OFFSET)

/* For orphan inode management */
#define F2FS_ORPHANS_PER_BLOCK	1020

#define GET_ORPHAN_BLOCKS(n)	(((n) + F2FS_ORPHANS_PER_BLOCK - 1) / F2FS_ORPHANS_PER_BLOCK)
struct f2fs_orphan_block {
	__le32 ino[F2FS_ORPHANS_PER_BLOCK];	/* inode numbers */
	__le32 reserved;	/* reserved */
	__le16 blk_addr;	/* block index in current CP */
	__le16 blk_count;	/* Number of orphan inode blocks in CP */
	__le32 entry_count;	/* Total number of orphan nodes in current CP */
	__le32 check_sum;	/* CRC32 for orphan inode block */
} ;

/* For NODE structure */
//<YUAN> move to f2fs.h
//struct f2fs_extent {
//	__le32 fofs;		/* start file offset of the extent */
//	__le32 blk_addr;	/* start block address of the extent */
//	__le32 len;		/* lengh of the extent */
//} ;

#define F2FS_NAME_LEN		255

/* max output length of pretty_print_filename() including null terminator */
#define F2FS_PRINT_NAMELEN	(4 * ((F2FS_NAME_LEN + 2) / 3) + 1)

/* 200 bytes for inline xattrs by default */
#define DEFAULT_INLINE_XATTR_ADDRS	50
#define DEF_ADDRS_PER_INODE	923			/* Address Pointers in an Inode */
//#define CUR_ADDRS_PER_INODE(inode)	(DEF_ADDRS_PER_INODE - __get_extra_isize(inode))
// for _inode == f2fs_inode_info
#define CUR_ADDRS_PER_INODE(_inode)		(DEF_ADDRS_PER_INODE - _inode->get_extra_isize())
// for _inode == f2fs_inode
#define CUR_ADDRS_PER_INODE_(_inode)	(DEF_ADDRS_PER_INODE - get_extra_isize(_inode))

#define DEF_NIDS_PER_INODE	5	/* Node IDs in an Inode */
#define ADDRS_PER_INODE(inode)	addrs_per_inode(inode)
#define DEF_ADDRS_PER_BLOCK	1018	/* Address Pointers in a Direct Block */
#define ADDRS_PER_BLOCK(inode)	addrs_per_block(inode)
#define NIDS_PER_BLOCK          1018	/* Node IDs in an Indirect Block */
#define ADDRS_PER_PAGE(page, inode)	\
	(IS_INODE(page) ? ADDRS_PER_INODE(inode) : ADDRS_PER_BLOCK(inode))

#define	NODE_DIR1_BLOCK		(DEF_ADDRS_PER_INODE + 1)
#define	NODE_DIR2_BLOCK		(DEF_ADDRS_PER_INODE + 2)
#define	NODE_IND1_BLOCK		(DEF_ADDRS_PER_INODE + 3)
#define	NODE_IND2_BLOCK		(DEF_ADDRS_PER_INODE + 4)
#define	NODE_DIND_BLOCK		(DEF_ADDRS_PER_INODE + 5)

#define F2FS_INLINE_XATTR	0x01	/* file inline xattr flag */
#define F2FS_INLINE_DATA	0x02	/* file inline data flag */
#define F2FS_INLINE_DENTRY	0x04	/* file inline dentry flag */
#define F2FS_DATA_EXIST		0x08	/* file inline data exist flag */
#define F2FS_INLINE_DOTS	0x10	/* file having implicit dot dentries */
#define F2FS_EXTRA_ATTR		0x20	/* file having extra attribute */
#define F2FS_PIN_FILE		0x40	/* file should not be gced */
#define F2FS_COMPRESS_RELEASED	0x80	/* file released compressed blocks */

#if !defined(offsetof)
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define F2FS_EXTRA_ISIZE_OFFSET			offsetof(f2fs_inode, _u._s.i_extra_isize)
//#define F2FS_TOTAL_EXTRA_ATTR_SIZE		(offsetof(struct f2fs_inode, _u._s.i_extra_end) - F2FS_EXTRA_ISIZE_OFFSET)
#define F2FS_TOTAL_EXTRA_ATTR_SIZE		sizeof(f2fs_inode::_u._s)

#define	F2FS_DEF_PROJID		0	/* default project ID */



#define DEF_MAX_INLINE_DATA	(sizeof(__le32) *			\
				(DEF_ADDRS_PER_INODE -			\
				DEFAULT_INLINE_XATTR_ADDRS -		\
				F2FS_TOTAL_EXTRA_ATTR_SIZE -		\
				DEF_INLINE_RESERVED_SIZE))

#define INLINE_DATA_OFFSET	(PAGE_CACHE_SIZE - sizeof(struct node_footer) \
				- sizeof(__le32)*(DEF_ADDRS_PER_INODE + 5 - \
				DEF_INLINE_RESERVED_SIZE))

#define DEF_DIR_LEVEL		0

/*
 * i_advise uses FADVISE_XXX_BIT. We can add additional hints later.
 */
#define FADVISE_COLD_BIT	0x01
#define FADVISE_LOST_PINO_BIT	0x02
#define FADVISE_ENCRYPT_BIT	0x04
#define FADVISE_ENC_NAME_BIT	0x08
#define FADVISE_KEEP_SIZE_BIT	0x10
#define FADVISE_HOT_BIT		0x20
#define FADVISE_VERITY_BIT	0x40	/* reserved */

//#define file_is_encrypt(fi)      ((fi)->i_advise & FADVISE_ENCRYPT_BIT)
//#define file_enc_name(fi)        ((fi)->i_advise & FADVISE_ENC_NAME_BIT)

#define F2FS_CASEFOLD_FL	0x40000000 /* Casefolded file */
//#define IS_CASEFOLDED(dir)     ((dir)->i_flags & F2FS_CASEFOLD_FL)

/* For NODE structure */
struct f2fs_extent
{
	__le32 fofs;		/* start file offset of the extent */
	__le32 blk;	/* start block address of the extent */
	__le32 len;		/* lengh of the extent */
};

/* inode flags */
#define F2FS_COMPR_FL		0x00000004 /* Compress file */
//<YUAN> raw data
struct f2fs_inode 
{
	__le16 i_mode;			/* file mode */
	__u8 i_advise;			/* file hints */
	__u8 i_inline;			/* file inline flags */
	__le32 i_uid;			/* user ID */
	__le32 i_gid;			/* group ID */
	__le32 i_links;			/* links count */
	__le64 i_size;			/* file size in bytes */
	__le64 i_blocks;		/* file size in blocks */
	__le64 i_atime;			/* access time */
	__le64 i_ctime;			/* change time */
	__le64 i_mtime;			/* modification time */
	__le32 i_atime_nsec;		/* access time in nano scale */
	__le32 i_ctime_nsec;		/* change time in nano scale */
	__le32 i_mtime_nsec;		/* modification time in nano scale */
	__le32 i_generation;		/* file version (for NFS) */
	union {
		__le32 i_current_depth;	/* only for directory depth */
		__le16 i_gc_failures;	/* # of gc failures on pinned file. only for regular files. */
	};
	__le32 i_xattr_nid;		/* nid to save xattr */
	__le32 i_flags;			/* file attributes */
	__le32 i_pino;			/* parent inode number */
	__le32 i_namelen;		/* file name length */
	__u8 i_name[F2FS_NAME_LEN];	/* file name for SPOR */
	__u8 i_dir_level;		/* dentry_level for large dir */

	struct f2fs_extent i_ext;	/* caching a largest extent */

	union 
	{
		struct
		{
			__le16 i_extra_isize;		/* extra inode attribute size */
			__le16 i_inline_xattr_size;	/* inline xattr size, unit: 4 bytes */
			__le32 i_projid;			/* project id */
			__le32 i_inode_checksum;	/* inode meta checksum */
			__le64 i_crtime;			/* creation time */
			__le32 i_crtime_nsec;		/* creation time in nano scale */
			__le64 i_compr_blocks;		/* # of compressed blocks */
			__u8 i_compress_algorithm;	/* compress algrithm */
			__u8 i_log_cluster_size;	/* log of cluster size */
//			__le16 i_padding;			/* padding */
			__le16 i_compress_flag;		/* compress flag; 0 bit: chksum flag; [10,15] bits: compress level */
			//<YUAN>避免vc 0长度成员变量报警。通过sizeof(f2fs_inode::_u._s)获得大小
			//__le32 i_extra_end[0];	/* for attribute size calculation */
		} _s; /**/;
		__le32 i_addr[DEF_ADDRS_PER_INODE];	/* Pointers to data blocks */
	} _u;
	__le32 i_nid[5];		/* direct(2), indirect(2), double_indirect(1) node id */
} ;


struct direct_node
{
	/* array of data block address */ //1018个，4BYTE一个地址，占4K空间，剩余6*4=24BYTE应该用于node_footer
	__le32 addr[DEF_ADDRS_PER_BLOCK];	
} ;

struct indirect_node {
	__le32 nid[NIDS_PER_BLOCK];	/* array of data block address */
} ;

enum {
	COLD_BIT_SHIFT = 0,
	FSYNC_BIT_SHIFT,
	DENT_BIT_SHIFT,
	OFFSET_BIT_SHIFT
};

#define OFFSET_BIT_MASK		(0x07)	/* (0x01 << OFFSET_BIT_SHIFT) - 1 */

#define XATTR_NODE_OFFSET	((((unsigned int)-1) << OFFSET_BIT_SHIFT) >> OFFSET_BIT_SHIFT)

struct node_footer {
	__le32 nid;		/* node id */
	__le32 ino;		/* inode nunmber */
	__le32 flag;		/* include cold/fsync/dentry marks and offset */
	__le64 cp_ver;		/* checkpoint version */
	__le32 next_blkaddr;	/* next node page block address */
} ;

// node的数据结构，可以是inode, direct node或者indirect node
struct f2fs_node 
{
	/* can be one of three types: inode, direct, and indirect types */
	union 
	{
		struct f2fs_inode i;
		struct direct_node dn;
		struct indirect_node in;
	};
	struct node_footer footer;
} ;

/* For NAT entries */
#define NAT_ENTRY_PER_BLOCK (PAGE_CACHE_SIZE / sizeof(struct f2fs_nat_entry))
//#define NAT_BLOCK_OFFSET(start_nid) (start_nid / NAT_ENTRY_PER_BLOCK)

#define DEFAULT_NAT_ENTRY_RATIO		20

struct f2fs_nat_entry {
	__u8 version;		/* latest version of cached nat entry */
	__le32 ino;		/* inode number */
	__le32 block_addr;	/* block address */
} ;

struct f2fs_nat_block
{
	f2fs_nat_entry entries[NAT_ENTRY_PER_BLOCK];
} ;

/* For SIT entries
 * Each segment is 2MB in size by default so that a bitmap for validity of there-in blocks should occupy 64 bytes, 512 bits. Not allow to change this. */
#define SIT_VBLOCK_MAP_SIZE 64
#define SIT_ENTRY_PER_BLOCK (PAGE_CACHE_SIZE / sizeof(struct f2fs_sit_entry))

/* F2FS uses 4 bytes to represent block address. As a result, supported size of disk is 16 TB and it equals to 16 * 1024 * 1024 / 2 segments. */
#define F2FS_MIN_SEGMENT      9 /* SB + 2 (CP + SIT + NAT) + SSA + MAIN */
#define F2FS_MAX_SEGMENT       ((16 * 1024 * 1024) / 2)
#define MAX_SIT_BITMAP_SIZE    (SEG_ALIGN(SIZE_ALIGN(F2FS_MAX_SEGMENT, \
						SIT_ENTRY_PER_BLOCK)) * \
						c.blks_per_seg / 8)

/* Note that f2fs_sit_entry->vblocks has the following bit-field information.
 * [15:10] : allocation type such as CURSEG_XXXX_TYPE
 * [9:0] : valid block count */
#define SIT_VBLOCKS_SHIFT	10
#define SIT_VBLOCKS_MASK	((1 << SIT_VBLOCKS_SHIFT) - 1)
#define GET_SIT_VBLOCKS(raw_sit)				\
	(le16_to_cpu((raw_sit)->vblocks) & SIT_VBLOCKS_MASK)
#define GET_SIT_TYPE(raw_sit)					\
	((le16_to_cpu((raw_sit)->vblocks) & ~SIT_VBLOCKS_MASK)	\
	 >> SIT_VBLOCKS_SHIFT)

// 一个segment的状况。一个segment包含512 blocks，这些block中，有多少有效block，以及有效block的bitmap
struct f2fs_sit_entry 
{
	__le16 vblocks;							/* reference above */
	__u8 valid_map[SIT_VBLOCK_MAP_SIZE];	/* bitmap for valid blocks */ //64个
	__le64 mtime;							/* segment age for cleaning */
} ;

// 一个sit block管理的segment数量
struct f2fs_sit_block 
{
	f2fs_sit_entry entries[SIT_ENTRY_PER_BLOCK];
} ;

/*
 * For segment summary
 *
 * One summary block contains exactly 512 summary entries, which represents
 * exactly 2MB segment by default. Not allow to change the basic units.
 *
 * NOTE: For initializing fields, you must use set_summary
 *
 * - If data page, nid represents dnode's nid
 * - If node page, nid represents the node page's nid.
 *
 * The ofs_in_node is used by only data page. It represents offset
 * from node's page's beginning to get a data block address.
 * ex) data_blkaddr = (block_t)(nodepage_start_address + ofs_in_node)
 */
#define ENTRIES_IN_SUM		512
#define	SUMMARY_SIZE		(7)	/* sizeof(struct summary) */
#define	SUM_FOOTER_SIZE		(5)	/* sizeof(struct summary_footer) */
#define SUM_ENTRY_SIZE		(SUMMARY_SIZE * ENTRIES_IN_SUM)

#define SUM_ENTRIES_SIZE	(SUMMARY_SIZE * ENTRIES_IN_SUM)

/* a summary entry for a 4KB-sized block in a segment */
struct f2fs_summary {
	__le32 nid;		/* parent node id */
	union {
		__u8 reserved[3];
		struct {
			__u8 version;		/* node version number */
			__le16 ofs_in_node;	/* block index in parent node */
		} _s;
	} _u;
} ;

/* summary block type, node or data, is stored to the summary_footer */
#define SUM_TYPE_NODE		(1)
#define SUM_TYPE_DATA		(0)

struct summary_footer {
	unsigned char entry_type;	/* SUM_TYPE_XXX */
	__le32 check_sum;		/* summary checksum */
} ;

#define SUM_JOURNAL_SIZE		(F2FS_BLKSIZE - SUM_FOOTER_SIZE - SUM_ENTRIES_SIZE)
#define NAT_JOURNAL_ENTRIES		((SUM_JOURNAL_SIZE - 2) / sizeof(nat_journal_entry))
#define NAT_JOURNAL_RESERVED	((SUM_JOURNAL_SIZE - 2) % sizeof(nat_journal_entry))
#define SIT_JOURNAL_ENTRIES		((SUM_JOURNAL_SIZE - 2) / sizeof(sit_journal_entry))
#define SIT_JOURNAL_RESERVED	((SUM_JOURNAL_SIZE - 2) % sizeof(sit_journal_entry))

/*
 * Reserved area should make size of f2fs_extra_info equals to
 * that of nat_journal and sit_journal.
 */
#define EXTRA_INFO_RESERVED	(SUM_JOURNAL_SIZE - 2 - 8)

/*
 * frequently updated NAT/SIT entries can be stored in the spare area in
 * summary blocks
 */
enum {
	NAT_JOURNAL = 0,
	SIT_JOURNAL
};

struct nat_journal_entry {
	__le32 nid;
	struct f2fs_nat_entry ne;
} ;

struct nat_journal {
	struct nat_journal_entry entries[NAT_JOURNAL_ENTRIES];
	__u8 reserved[NAT_JOURNAL_RESERVED];
} ;

struct sit_journal_entry {
	__le32 segno;
	struct f2fs_sit_entry se;
} ;

struct sit_journal {
	struct sit_journal_entry entries[SIT_JOURNAL_ENTRIES];
	__u8 reserved[SIT_JOURNAL_RESERVED];
} ;

struct f2fs_extra_info {
	__le64 kbytes_written;
	__u8 reserved[EXTRA_INFO_RESERVED];
} ;

struct f2fs_journal {
	union {
		__le16 n_nats;
		__le16 n_sits;
	};
	/* spare area is used by NAT or SIT journals or extra info */
	union {
		struct nat_journal nat_j;
		struct sit_journal sit_j;
		struct f2fs_extra_info info;
	};
} ;

//<YUAN> raw data
/* 4KB-sized summary block structure */
struct f2fs_summary_block 
{
	f2fs_summary entries[ENTRIES_IN_SUM];
	f2fs_journal journal;
	summary_footer footer;
} ;

/*
 * For directory operations
 */
#define F2FS_DOT_HASH		0
#define F2FS_DDOT_HASH		F2FS_DOT_HASH
#define F2FS_MAX_HASH		(~((0x3ULL) << 62))
#define F2FS_HASH_COL_BIT	((0x1ULL) << 63)

typedef __le32	f2fs_hash_t;

/* One directory entry slot covers 8bytes-long file name */
#define F2FS_SLOT_LEN		8
#define F2FS_SLOT_LEN_BITS	3

#define GET_DENTRY_SLOTS(x)	((x + F2FS_SLOT_LEN - 1) >> F2FS_SLOT_LEN_BITS)

/* the number of dentry in a block */
#define NR_DENTRY_IN_BLOCK	214

/* MAX level for dir lookup */
#define MAX_DIR_HASH_DEPTH	63

/* MAX buckets in one level of dir */
#define MAX_DIR_BUCKETS		(1 << ((MAX_DIR_HASH_DEPTH / 2) - 1))

/*
 * space utilization of regular dentry and inline dentry (w/o extra reservation)
 *		regular dentry		inline dentry (def)	inline dentry (min)
 * bitmap	1 * 27 = 27		1 * 23 = 23		1 * 1 = 1
 * reserved	1 * 3 = 3		1 * 7 = 7		1 * 1 = 1
 * dentry	11 * 214 = 2354		11 * 182 = 2002		11 * 2 = 22
 * filename	8 * 214 = 1712		8 * 182 = 1456		8 * 2 = 16
 * total	4096			3488			40
 *
 * Note: there are more reserved space in inline dentry than in regular
 * dentry, when converting inline dentry we should handle this carefully.
 */
#define NR_DENTRY_IN_BLOCK	214	/* the number of dentry in a block */
#define SIZE_OF_DIR_ENTRY	11	/* by byte */
#define SIZE_OF_DENTRY_BITMAP	((NR_DENTRY_IN_BLOCK + BITS_PER_BYTE - 1) / \
					BITS_PER_BYTE)
#define SIZE_OF_RESERVED	(PAGE_SIZE - ((SIZE_OF_DIR_ENTRY + \
				F2FS_SLOT_LEN) * \
				NR_DENTRY_IN_BLOCK + SIZE_OF_DENTRY_BITMAP))
#define MIN_INLINE_DENTRY_SIZE		40	/* just include '.' and '..' entries */


#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12
#define DT_WHT		14


/* One directory entry slot representing F2FS_SLOT_LEN-sized file name */
struct f2fs_dir_entry 
{
	__le32 hash_code;	/* hash code of file name */
	__le32 ino;			/* inode number */
	__le16 name_len;	/* lengh of file name */
	__u8 file_type;		/* file type */
} ;

/* 4KB-sized directory entry block */
struct f2fs_dentry_block {
	/* validity bitmap for directory entries in each block */
	__u8 dentry_bitmap[SIZE_OF_DENTRY_BITMAP];
	__u8 reserved[SIZE_OF_RESERVED];
	struct f2fs_dir_entry dentry[NR_DENTRY_IN_BLOCK];
	__u8 filename[NR_DENTRY_IN_BLOCK][F2FS_SLOT_LEN];
} ;
#pragma pack(pop)

/* for inline stuff */
#define DEF_INLINE_RESERVED_SIZE	1

/* for inline dir */
#define NR_INLINE_DENTRY(node)	(MAX_INLINE_DATA(node) * BITS_PER_BYTE / \
				((SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN) * BITS_PER_BYTE + 1))

//#define INLINE_DENTRY_BITMAP_SIZE(node)	((NR_INLINE_DENTRY(node) + BITS_PER_BYTE - 1) / BITS_PER_BYTE)
#define INLINE_RESERVED_SIZE(node)	(MAX_INLINE_DATA(node) - \
				((SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN) * NR_INLINE_DENTRY(node) + \
				INLINE_DENTRY_BITMAP_SIZE(node)))

/* file types used in inode_info->flags */
enum FILE_TYPE {
	F2FS_FT_UNKNOWN,
	F2FS_FT_REG_FILE,
	F2FS_FT_DIR,
	F2FS_FT_CHRDEV,
	F2FS_FT_BLKDEV,
	F2FS_FT_FIFO,
	F2FS_FT_SOCK,
	F2FS_FT_SYMLINK,
	F2FS_FT_MAX,
	/* added for fsck */
	F2FS_FT_ORPHAN,
	F2FS_FT_XATTR,
	F2FS_FT_LAST_FILE_TYPE = F2FS_FT_XATTR,
};

#define S_SHIFT 12
#define	F2FS_DEF_PROJID		0	/* default project ID */


#define LINUX_S_IFMT  00170000
#define LINUX_S_IFREG  0100000
#define LINUX_S_ISREG(m)	(((m) & LINUX_S_IFMT) == LINUX_S_IFREG)

/* from f2fs/segment.h */
//enum {
//	LFS = 0,
//	SSR
//};

extern int utf8_to_utf16(UINT16 *, const char *, size_t, size_t);
extern int utf16_to_utf8(char *, const UINT16 *, size_t, size_t);
extern int log_base_2(UINT32);
//extern unsigned int addrs_per_inode(struct f2fs_inode *);
extern unsigned int addrs_per_block(struct f2fs_inode *);
extern UINT64 f2fs_max_file_offset(struct f2fs_inode *);
//extern __u32 f2fs_inode_chksum(struct f2fs_node *);
extern __u32 f2fs_checkpoint_chksum(struct f2fs_checkpoint *);
//extern int write_inode(struct f2fs_node *, UINT64);

extern int get_bits_in_byte(unsigned char n);
extern int test_and_set_bit_le(UINT32, UINT8 *);
extern int test_and_clear_bit_le(UINT32, UINT8 *);
extern int test_bit_le(UINT32, const UINT8 *);
extern int f2fs_test_bit(unsigned int, const char *);
//extern int f2fs_set_bit(unsigned int, char *);
//extern int f2fs_clear_bit(unsigned int, char *);
extern UINT64 find_next_bit_le(const UINT8 *, UINT64, UINT64);
extern UINT64 find_next_zero_bit_le(const UINT8 *, UINT64, UINT64);

extern UINT32 f2fs_cal_crc32(UINT32, const void *, size_t);
extern int f2fs_crc_valid(UINT32 blk_crc, void *buf, int len);

//extern void f2fs_init_configuration(f2fs_configuration & c);
extern int f2fs_devs_are_umounted(void);
extern int f2fs_dev_is_writable(f2fs_configuration & config);
extern int f2fs_dev_is_umounted(char *);
//extern int f2fs_get_device_info(void);
extern unsigned int calc_extra_isize(f2fs_configuration & config);
extern int get_device_info(f2fs_configuration& config, int);
extern int f2fs_init_sparse_file(void);
extern void f2fs_release_sparse_resource(void);
extern int f2fs_finalize_device(void);
extern int f2fs_fsync_device(void);

//extern void dcache_init(void);
//extern void dcache_release(void);

//extern int dev_read(void *, __u64, size_t);
//#ifdef POSIX_FADV_WILLNEED
//extern int dev_readahead(__u64, size_t);
//#else
//extern int dev_readahead(__u64, size_t UNUSED(len));
//#endif
//extern int dev_write(void *, __u64, size_t);
//extern int dev_write_block(void *, __u64);
//extern int dev_write_dump(void *, __u64, size_t);
/* All bytes in the buffer must be 0 use dev_fill(). */
//extern int dev_fill(void *, __u64, size_t);
//extern int dev_fill_block(void *, __u64);
//
//extern int dev_read_block(void *, __u64);
//extern int dev_reada_block(__u64);

//extern int dev_read_version(void *, __u64, size_t);
extern void get_kernel_version(char *);
extern void get_kernel_uname_version(char *);
f2fs_hash_t f2fs_dentry_hash(int, int, const unsigned char *, int);

static inline bool f2fs_has_extra_isize(struct f2fs_inode *inode)
{
	return (inode->i_inline & F2FS_EXTRA_ATTR);
}

//#define get_extra_isize(node)	__get_extra_isize(&node->i)

#define F2FS_ZONED_NONE		0
#define F2FS_ZONED_HA		1
#define F2FS_ZONED_HM		2

#ifdef HAVE_LINUX_BLKZONED_H

/* Let's just use v2, since v1 should be compatible with v2 */
#define BLK_ZONE_REP_CAPACITY   (1 << 0)
struct blk_zone_v2 {
	__u64   start;          /* Zone start sector */
	__u64   len;            /* Zone length in number of sectors */
	__u64   wp;             /* Zone write pointer position */
	__u8    type;           /* Zone type */
	__u8    cond;           /* Zone condition */
	__u8    non_seq;        /* Non-sequential write resources active */
	__u8    reset;          /* Reset write pointer recommended */
	__u8    resv[4];
	__u64   capacity;       /* Zone capacity in number of sectors */
	__u8    reserved[24];
};
#define blk_zone blk_zone_v2

struct blk_zone_report_v2 {
	__u64   sector;
	__u32   nr_zones;
	__u32   flags;
	struct blk_zone zones[0];
};
#define blk_zone_report blk_zone_report_v2

#define blk_zone_type(z)        (z)->type
#define blk_zone_conv(z)	((z)->type == BLK_ZONE_TYPE_CONVENTIONAL)
#define blk_zone_seq_req(z)	((z)->type == BLK_ZONE_TYPE_SEQWRITE_REQ)
#define blk_zone_seq_pref(z)	((z)->type == BLK_ZONE_TYPE_SEQWRITE_PREF)
#define blk_zone_seq(z)		(blk_zone_seq_req(z) || blk_zone_seq_pref(z))

static inline const char *
blk_zone_type_str(struct blk_zone *blkz)
{
	switch (blk_zone_type(blkz)) {
	case BLK_ZONE_TYPE_CONVENTIONAL:
		return( "Conventional" );
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
		return( "Sequential-write-required" );
	case BLK_ZONE_TYPE_SEQWRITE_PREF:
		return( "Sequential-write-preferred" );
	}
	return( "Unknown-type" );
}

#define blk_zone_cond(z)	(z)->cond

static inline const char *
blk_zone_cond_str(struct blk_zone *blkz)
{
	switch (blk_zone_cond(blkz)) {
	case BLK_ZONE_COND_NOT_WP:
		return "Not-write-pointer";
	case BLK_ZONE_COND_EMPTY:
		return "Empty";
	case BLK_ZONE_COND_IMP_OPEN:
		return "Implicit-open";
	case BLK_ZONE_COND_EXP_OPEN:
		return "Explicit-open";
	case BLK_ZONE_COND_CLOSED:
		return "Closed";
	case BLK_ZONE_COND_READONLY:
		return "Read-only";
	case BLK_ZONE_COND_FULL:
		return "Full";
	case BLK_ZONE_COND_OFFLINE:
		return "Offline";
	}
	return "Unknown-cond";
}

/*
 * Handle kernel zone capacity support
 */
#define blk_zone_empty(z)	(blk_zone_cond(z) == BLK_ZONE_COND_EMPTY)
#define blk_zone_sector(z)	(z)->start
#define blk_zone_length(z)	(z)->len
#define blk_zone_wp_sector(z)	(z)->wp
#define blk_zone_need_reset(z)	(int)(z)->reset
#define blk_zone_non_seq(z)	(int)(z)->non_seq
#define blk_zone_capacity(z, f) ((f & BLK_ZONE_REP_CAPACITY) ? \
					(z)->capacity : (z)->len)

#endif

extern int f2fs_get_zoned_model(f2fs_configuration & c, int);
extern int f2fs_get_zone_blocks(f2fs_configuration & c, int);
extern int f2fs_report_zone(int, UINT64, void *);
typedef int (report_zones_cb_t)(int i, void *, void *);
extern int f2fs_report_zones(int, report_zones_cb_t *, void *);
extern int f2fs_check_zones(int);
int f2fs_reset_zone(int, void *);
extern int f2fs_reset_zones(int);
extern uint32_t f2fs_get_usable_segments(struct f2fs_super_block *sb);

#define SIZE_ALIGN(val, size)	(((val) + (size) - 1) / (size))
#define SEG_ALIGN(blks)		SIZE_ALIGN(blks, c.blks_per_seg)
#define ZONE_ALIGN(blks)	SIZE_ALIGN(blks, c.blks_per_seg * \
					c.segs_per_zone)

static inline double get_best_overprovision(struct f2fs_super_block *sb)
{
	double reserved, ovp, candidate, end, diff, space;
	double max_ovp = 0, max_space = 0;
	UINT32 usable_main_segs = f2fs_get_usable_segments(sb);

	if (get_sb(segment_count_main) < 256) 
	{
		candidate = 10;
		end = 95;
		diff = 5;
	} 
	else 
	{
		candidate = 0.01;
		end = 10;
		diff = 0.01;
	}

	for (; candidate <= end; candidate += diff) 
	{
#if 0 //<DEBUG>
		UINT sec_count = get_sb(section_count);
		wprintf_s(L"[debug] usable_main_segs=%d, section_count=%d, DIV_ROUND_UP=%d\n", usable_main_segs, sec_count, DIV_ROUND_UP(usable_main_segs, sec_count));
#endif
		reserved = (2 * (100 / candidate + 1) + 6) * DIV_ROUND_UP(usable_main_segs, get_sb(section_count));
		ovp = (usable_main_segs - reserved) * candidate / 100;
		space = usable_main_segs - reserved - ovp;
		if (max_space < space) 
		{
			max_space = space;
			max_ovp = candidate;
		}
	}
	return max_ovp;
}

static inline __le64 get_cp_crc(struct f2fs_checkpoint *cp)
{
	UINT64 cp_ver = get_cp(checkpoint_ver);
	size_t crc_offset = get_cp(checksum_offset);
	UINT32 crc = le32_to_cpu(*(__le32 *)((unsigned char *)cp +	crc_offset));

	cp_ver |= ((UINT64)crc << 32);
	return cpu_to_le64(cp_ver);
}

static inline int exist_qf_ino(struct f2fs_super_block *sb)
{
	int i;
	for (i = 0; i < F2FS_MAX_QUOTAS; i++)
	{
		if (sb->qf_ino[i])	return 1;
	}
	return 0;
}

static inline int is_qf_ino(struct f2fs_super_block *sb, nid_t ino)
{
	int i;

	for (i = 0; i < F2FS_MAX_QUOTAS; i++)
		if (sb->qf_ino[i] == ino)
			return 1;
	return 0;
}

static inline void show_version(const char *prog)
{
#if defined(F2FS_TOOLS_VERSION) && defined(F2FS_TOOLS_DATE)
	printf_s(0, "%s %s (%s)\n", prog, F2FS_TOOLS_VERSION, F2FS_TOOLS_DATE);
#else
	printf_s(0, "%s -- version not supported\n", prog);
#endif
}

struct feature {
	char *name;
	UINT32  mask;
};

#define INIT_FEATURE_TABLE						\
struct feature feature_table[] = {					\
	{ "encrypt",				F2FS_FEATURE_ENCRYPT },		\
	{ "extra_attr",				F2FS_FEATURE_EXTRA_ATTR },	\
	{ "project_quota",			F2FS_FEATURE_PRJQUOTA },	\
	{ "inode_checksum",			F2FS_FEATURE_INODE_CHKSUM },	\
	{ "flexible_inline_xattr",	F2FS_FEATURE_FLEXIBLE_INLINE_XATTR },\
	{ "quota",			F2FS_FEATURE_QUOTA_INO },	\
	{ "inode_crtime",		F2FS_FEATURE_INODE_CRTIME },	\
	{ "lost_found",			F2FS_FEATURE_LOST_FOUND },	\
	{ "verity",			F2FS_FEATURE_VERITY },	/* reserved */ \
	{ "sb_checksum",		F2FS_FEATURE_SB_CHKSUM },	\
	{ "casefold",			F2FS_FEATURE_CASEFOLD },	\
	{ "compression",		F2FS_FEATURE_COMPRESSION },	\
	{ "ro",				F2FS_FEATURE_RO},		\
	{ NULL,				0x0},				\
};

static inline UINT32 feature_map(struct feature *table, char *feature)
{
	struct feature *p;
	for (p = table; p->name && strcmp(p->name, feature); p++)		;
	return p->mask;
}

#if 0 // move to f2fs-filesystem.h
static inline int set_feature_bits(struct feature *table, char *features)
{
	UINT32 mask = feature_map(table, features);
	if (mask) {	c.feature |= cpu_to_le32(mask);	} 
	else 
	{
		MSG(0, "Error: Wrong features %s\n", features);
		return -1;
	}
	return 0;
}

static inline int parse_feature(struct feature *table, const char *features)
{
	char *buf, *sub, *next;

	buf = _strdup(features);
	if (!buf)		return -1;

	for (sub = buf; sub && *sub; sub = next ? next + 1 : NULL) 
	{
		/* Skip the beginning blanks */
		while (*sub && *sub == ' ')		sub++;
		next = sub;
		/* Skip a feature word */
		while (*next && *next != ' ' && *next != ',')	next++;

		if (*next == 0)		next = NULL;
		else			*next = 0;

		if (set_feature_bits(table, sub)) 
		{
			free(buf);
			return -1;
		}
	}
	free(buf);
	return 0;
}
#endif

static inline int parse_root_owner(char *ids, UINT32 *root_uid, UINT32 *root_gid)
{
	char *uid = ids;
	char *gid = NULL;
	int i;

	/* uid:gid */
	for (i = 0; i < strlen(ids) - 1; i++)
		if (*(ids + i) == ':')
			gid = ids + i + 1;
	if (!gid) return -1;

	*root_uid = atoi(uid);
	*root_gid = atoi(gid);
	return 0;
}

/*
 * NLS definitions
 */
struct f2fs_nls_table {
	int version;
	const struct f2fs_nls_ops *ops;
};

struct f2fs_nls_ops {
	int (*casefold)(const struct f2fs_nls_table *charset,
			const unsigned char *str, size_t len,
			unsigned char *dest, size_t dlen);
};

extern const struct f2fs_nls_table *f2fs_load_nls_table(int encoding);
#define F2FS_ENC_UTF8_12_0	1

extern int f2fs_str2encoding(const char *string);
extern const char *f2fs_encoding2str(const int encoding);
extern int f2fs_get_encoding_flags(int encoding);
extern int f2fs_str2encoding_flags(char **param, __u16 *flags);
























#endif	/*__F2FS_FS_H */
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

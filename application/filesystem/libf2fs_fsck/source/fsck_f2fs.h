/**
 * f2fs.h
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#pragma once

#include <include/f2fs.h>

#if 0

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#ifdef HAVE_MACH_TIME_H
#include <mach/mach_time.h>
#endif
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <assert.h>

#include "f2fs_fs.h"
#endif 

extern struct f2fs_configuration c;


#define EXIT_ERR_CODE		(-1)
//#define ver_after(a, b) (typecheck(unsigned long long, a) &&            \
//		typecheck(unsigned long long, b) &&                     \
//		((long long)((a) - (b)) > 0))

//#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member) ({			\
	const typeof(((type *)0)->member) * __mptr = (ptr);	\
	(type *)((char *)__mptr - offsetof(type, member)); })

//struct list_head {
//	struct list_head *next, *prev;
//};

//static inline void __list_add(struct list_head *new_iten, struct list_head *prev, struct list_head *next)
//{
//	next->prev = new_iten;
//	new_iten->next = next;
//	new_iten->prev = prev;
//	prev->next = new_iten;
//}
//
//static inline void __list_del(struct list_head * prev, struct list_head * next)
//{
//	next->prev = prev;
//	prev->next = next;
//}

//static inline void list_del(struct list_head *entry)
//{
//	__list_del(entry->prev, entry->next);
//}

//static inline void list_add_tail(struct list_head *new_iten, struct list_head *head)
//{
//	__list_add(new_iten, head->prev, head);
//}

#define LIST_HEAD_INIT(name) { &(name), &(name) }

//#define list_entry(ptr, type, member)					container_of(ptr, type, member)
#define list_first_entry(ptr, type, member)				list_entry((ptr)->next, type, member)
//#define list_next_entry(pos, member)					list_entry((pos)->member.next, typeof(*(pos)), member)

//#define list_for_each_entry(pos, head, member)				\
//	for (pos = list_first_entry(head, typeof(*pos), member);	\
//		&pos->member != (head);					\
//		pos = list_next_entry(pos, member))
//
//#define list_for_each_entry_safe(pos, n, head, member)			\
//	for (pos = list_first_entry(head, typeof(*pos), member),	\
//		n = list_next_entry(pos, member);			\
//		&pos->member != (head);					\
//		pos = n, n = list_next_entry(n, member))

/* indicate meta/data type */
//enum {
//	META_CP,
//	META_NAT,
//	META_SIT,
//	META_SSA,
//	META_MAX,
//	META_POR,
//};

#define MAX_RA_BLOCKS	64

//enum {
//	NAT_BITMAP,
//	SIT_BITMAP
//};

//struct node_info {
//	nid_t nid;
//	nid_t ino;
//	u32 blk_addr;
//	unsigned char version;
//};

struct fsck_f2fs_nm_info {
	block_t nat_blkaddr;
	block_t nat_blocks;
	nid_t max_nid;
	nid_t init_scan_nid;
	nid_t next_scan_nid;

	unsigned int nat_cnt;
	unsigned int fcnt;

	char *nat_bitmap;
	int bitmap_size;
	char *nid_bitmap;
};


struct fsck_seg_entry {
	unsigned short valid_blocks;    /* # of valid blocks */
	unsigned short ckpt_valid_blocks;	/* # of valid blocks last cp, for recovered data/node */
	unsigned char *cur_valid_map;   /* validity bitmap of blocks */
	unsigned char *ckpt_valid_map;	/* validity bitmap of blocks last cp, for recovered data/node */
	unsigned char type;             /* segment type like CURSEG_XXX_TYPE */
	unsigned char orig_type;        /* segment type like CURSEG_XXX_TYPE */
	unsigned char ckpt_type;        /* segment type like CURSEG_XXX_TYPE , for recovered data/node */
	unsigned long long mtime;       /* modification time of the segment */
	int dirty;
};

//struct sec_entry {
//	unsigned int valid_blocks;      /* # of valid blocks in a section */
//};

struct fsck_sit_info {
	block_t sit_base_addr;          /* start block address of SIT area */
	block_t sit_blocks;             /* # of blocks used by SIT area */
	block_t written_valid_blocks;   /* # of valid blocks in main area */
	unsigned char *bitmap;		/* all bitmaps pointer */
	char *sit_bitmap;               /* SIT bitmap pointer */
	unsigned int bitmap_size;       /* SIT bitmap size */

	unsigned long *dirty_sentries_bitmap;   /* bitmap for dirty sentries */
	unsigned int dirty_sentries;            /* # of dirty sentries */
	unsigned int sents_per_block;           /* # of SIT entries per block */
	fsck_seg_entry *sentries;             /* SIT segment-level cache */
	struct sec_entry *sec_entries;          /* SIT section-level cache */

	unsigned long long elapsed_time;        /* elapsed time after mount */
	unsigned long long mounted_time;        /* mount time */
	unsigned long long min_mtime;           /* min. modification time */
	unsigned long long max_mtime;           /* max. modification time */
};

struct fsck_curseg_info {
	struct f2fs_summary_block *sum_blk;     /* cached summary block */
	unsigned char alloc_type;               /* current allocation type */
	unsigned int segno;                     /* current segment number */
	unsigned short next_blkoff;             /* next block offset to write */
	unsigned int zone;                      /* current zone number */
	unsigned int next_segno;                /* preallocated segment */
};

struct fsck_f2fs_sm_info {
	fsck_sit_info *sit_info;
	fsck_curseg_info *curseg_array;

	block_t seg0_blkaddr;
	block_t main_blkaddr;
	block_t ssa_blkaddr;

	unsigned int segment_count;
	unsigned int main_segments;
	unsigned int reserved_segments;
	unsigned int ovp_segments;
};
#if 0

struct f2fs_dentry_ptr {
	struct inode *inode;
	u8 *bitmap;
	struct f2fs_dir_entry *dentry;
	__u8 (*filename)[F2FS_SLOT_LEN];
	int max;
	int nr_bitmap;
};
#endif

struct fsck_dentry {
	char *path;
	char *full_path;
	const u8 *name;
	int len;
	char *link;
	unsigned long size;
	u8 file_type;
	u16 mode;
	u16 uid;
	u16 gid;
	u32 *inode;
	u32 mtime;
	char *secon;
	uint64_t capabilities;
	nid_t ino;
	nid_t pino;
	u64 from_devino;
};

/* different from dnode_of_data in kernel */
struct fsck_dnode_of_data {
	struct f2fs_node *inode_blk;	/* inode page */
	struct f2fs_node *node_blk;	/* cached direct node page */
	nid_t nid;
	unsigned int ofs_in_node;
	block_t data_blkaddr;
	block_t node_blkaddr;
	int idirty, ndirty;
};

struct hardlink_cache_entry {
	u64 from_devino;
	nid_t to_ino;
	int nbuild;
};

struct fsck_f2fs_sb_info {
	struct f2fs_fsck *fsck;

	struct f2fs_super_block *raw_super;
	struct fsck_f2fs_nm_info *nm_info;
	fsck_f2fs_sm_info *sm_info;
	struct f2fs_checkpoint * ckpt;
	int cur_cp;

	struct list_head orphan_inode_list;
	unsigned int n_orphans;

	/* basic file system units */
	unsigned int log_sectors_per_block;     /* log2 sectors per block */
	unsigned int log_blocksize;             /* log2 block size */
	unsigned int blocksize;                 /* block size */
	unsigned int root_ino_num;              /* root inode number*/
	unsigned int node_ino_num;              /* node inode number*/
	unsigned int meta_ino_num;              /* meta inode number*/
	unsigned int log_blocks_per_seg;        /* log2 blocks per segment */
	unsigned int blocks_per_seg;            /* blocks per segment */
	unsigned int segs_per_sec;              /* segments per section */
	unsigned int secs_per_zone;             /* sections per zone */
	unsigned int total_sections;            /* total section count */
	unsigned int total_node_count;          /* total node block count */
	unsigned int total_valid_node_count;    /* valid node block count */
	unsigned int total_valid_inode_count;   /* valid inode count */
	int active_logs;                        /* # of active logs */

	block_t user_block_count;               /* # of user blocks */
	block_t total_valid_block_count;        /* # of valid blocks */
	block_t alloc_valid_block_count;        /* # of allocated blocks */
	block_t last_valid_block_count;         /* for recovery */
	u32 s_next_generation;                  /* for NFS support */

	unsigned int cur_victim_sec;            /* current victim section num */
	u32 free_segments;

	int cp_backuped;			/* backup valid checkpoint */

	/* true if late_build_segment_manger() is called */
	bool seg_manager_done;

	/* keep track of hardlinks so we can recreate them */
	void *hardlink_cache;

// for debug
	int m_call_depth;

public:
	CF2fsFileSystem* m_fs;
public:
//	fsck_f2fs_sb_info(CF2fsFileSystem* fs) : m_fs(fs){}
	CF2fsFileSystem* GetFs(void) { return m_fs; }
	f2fs_checkpoint* F2FS_CKPT(void) { return ckpt; }
	f2fs_fsck* F2FS_FSCK(void) {return fsck;	}
	f2fs_super_block* F2FS_RAW_SUPER(void) { return raw_super; }
	fsck_f2fs_sm_info* SM_I(void) { return sm_info; }
	fsck_f2fs_nm_info* NM_I(void) { return nm_info; }
	unsigned int MAIN_SEGS(void) {	return sm_info->main_segments;	}


	unsigned int  F2FS_ROOT_INO() { return root_ino_num; }
	unsigned int  F2FS_NODE_INO() { return node_ino_num; }
	unsigned int  F2FS_META_INO() {	return meta_ino_num; }

	inline block_t __cp_payload(void)
	{
		return le32_to_cpu(F2FS_RAW_SUPER()->cp_payload);
	}

	inline unsigned long __bitmap_size(int flag);

	inline void* __bitmap_ptr(int flag);

	inline block_t start_sum_block(void);

	inline block_t sum_blk_addr(int base, int type);
};

static inline struct f2fs_fsck *F2FS_FSCK(fsck_f2fs_sb_info *sbi)
{
	return (struct f2fs_fsck *)(sbi->fsck);
}

static inline fsck_sit_info *SIT_I(fsck_f2fs_sb_info *sbi)
{
	return (fsck_sit_info *)(sbi->SM_I()->sit_info);
}


static inline void *inline_data_addr(struct f2fs_node *node_blk)
{
	int ofs = get_extra_isize(&node_blk->i) + DEF_INLINE_RESERVED_SIZE;

	return (void *)&(node_blk->i._u.i_addr[ofs]);
}

static inline unsigned int ofs_of_node(struct f2fs_node *node_blk)
{
	unsigned flag = le32_to_cpu(node_blk->footer.flag);
	return flag >> OFFSET_BIT_SHIFT;
}

//static inline unsigned long long cur_cp_version(struct f2fs_checkpoint *cp)
//{
//	return le64_to_cpu(cp->checkpoint_ver);
//}
//
//static inline __u64 cur_cp_crc(struct f2fs_checkpoint *cp)
//{
//	size_t crc_offset = le32_to_cpu(cp->checksum_offset);
//	return le32_to_cpu(*((__le32 *)((unsigned char *)cp + crc_offset)));
//}


static inline bool is_set_ckpt_flags(struct f2fs_checkpoint *cp, unsigned int f)
{
	unsigned int ckpt_flags = le32_to_cpu(cp->ckpt_flags);
	return ckpt_flags & f ? 1 : 0;
}


inline unsigned long fsck_f2fs_sb_info::__bitmap_size(int flag)
{
	f2fs_checkpoint* ckpt = F2FS_CKPT();
	/* return NAT or SIT bitmap */
	if (flag == NAT_BITMAP)			return le32_to_cpu(ckpt->nat_ver_bitmap_bytesize);
	else if (flag == SIT_BITMAP)	return le32_to_cpu(ckpt->sit_ver_bitmap_bytesize);
	return 0;
}

inline void* fsck_f2fs_sb_info::__bitmap_ptr(int flag)
{
	//		struct f2fs_checkpoint* ckpt = F2FS_CKPT();
	int offset;

	if (is_set_ckpt_flags(ckpt, CP_LARGE_NAT_BITMAP_FLAG)) {
		unsigned int chksum_size = 0;
		offset = (flag == SIT_BITMAP) ? le32_to_cpu(ckpt->nat_ver_bitmap_bytesize) : 0;
		if (le32_to_cpu(ckpt->checksum_offset) == CP_MIN_CHKSUM_OFFSET)		chksum_size = sizeof(__le32);
		return &ckpt->sit_nat_version_bitmap + offset + chksum_size;
	}

	if (le32_to_cpu(F2FS_RAW_SUPER()->cp_payload) > 0) {
		if (flag == NAT_BITMAP)		return &ckpt->sit_nat_version_bitmap;
		else						return ((char*)ckpt + F2FS_BLKSIZE);
	}
	else {
		offset = (flag == NAT_BITMAP) ? le32_to_cpu(ckpt->sit_ver_bitmap_bytesize) : 0;
		return &ckpt->sit_nat_version_bitmap + offset;
	}
}


static inline block_t __start_cp_addr(fsck_f2fs_sb_info *sbi)
{
	block_t start_addr = le32_to_cpu(sbi->F2FS_RAW_SUPER()->cp_blkaddr);

	if (sbi->cur_cp == 2)	start_addr += sbi->blocks_per_seg;
	return start_addr;
}

inline block_t fsck_f2fs_sb_info::start_sum_block(void)
{
	return __start_cp_addr(this) + le32_to_cpu(ckpt->cp_pack_start_sum);
}
inline block_t fsck_f2fs_sb_info::sum_blk_addr(int base, int type)
{
	return __start_cp_addr(this) + le32_to_cpu(ckpt->cp_pack_total_block_count) - (base + 1) + type;
}

static inline block_t __start_sum_addr(fsck_f2fs_sb_info *sbi)
{
	return le32_to_cpu(sbi->F2FS_CKPT()->cp_pack_start_sum);
}

static inline block_t __end_block_addr(fsck_f2fs_sb_info *sbi)
{
	block_t end = sbi->SM_I()->main_blkaddr;
	return (block_t)(end + le64_to_cpu(sbi->F2FS_RAW_SUPER()->block_count));
}

#define GET_ZONENO_FROM_SEGNO(sbi, segno)                               \
	((segno / sbi->segs_per_sec) / sbi->secs_per_zone)

//#define IS_DATASEG(t)                                                   \
//	((t == CURSEG_HOT_DATA) || (t == CURSEG_COLD_DATA) ||           \
//	 (t == CURSEG_WARM_DATA))

//#define IS_NODESEG(t)                                                   \
//	((t == CURSEG_HOT_NODE) || (t == CURSEG_COLD_NODE) ||           \
//	 (t == CURSEG_WARM_NODE))

#define MAIN_BLKADDR(sbi)						\
	(sbi->SM_I() ? sbi->SM_I()->main_blkaddr :				\
		le32_to_cpu(sbi->F2FS_RAW_SUPER()->main_blkaddr))
#define SEG0_BLKADDR(sbi)						\
	(sbi->SM_I() ? sbi->SM_I()->seg0_blkaddr :				\
		le32_to_cpu(sbi->F2FS_RAW_SUPER()->segment0_blkaddr))

#define GET_SUM_BLKADDR(sbi, segno)					\
	((sbi->sm_info->ssa_blkaddr) + segno)

//#define GET_SEGOFF_FROM_SEG0(sbi, blk_addr)				\
//	((blk_addr) - sbi->SM_I()->seg0_blkaddr)

//#define GET_SEGNO_FROM_SEG0(sbi, blk_addr)				\
//	(GET_SEGOFF_FROM_SEG0(sbi, blk_addr) >> sbi->log_blocks_per_seg)

//#define GET_BLKOFF_FROM_SEG0(sbi, blk_addr)				\
//	(GET_SEGOFF_FROM_SEG0(sbi, blk_addr) & (sbi->blocks_per_seg - 1))

//#define GET_SEC_FROM_SEG(sbi, segno)					\
//	((segno) / (sbi)->segs_per_sec)
#define GET_SEG_FROM_SEC(sbi, secno)					\
	((secno) * (sbi)->segs_per_sec)

#define FREE_I_START_SEGNO(sbi)						\
	GET_SEGNO_FROM_SEG0(sbi, sbi->SM_I()->main_blkaddr)
//#define GET_R2L_SEGNO(sbi, segno)	(segno + FREE_I_START_SEGNO(sbi))

//#define MAIN_SEGS(sbi)	(sbi->SM_I()->main_segments)
#define TOTAL_BLKS(sbi)	(TOTAL_SEGS(sbi) << (sbi)->log_blocks_per_seg)
#define MAX_BLKADDR(sbi)	(SEG0_BLKADDR(sbi) + TOTAL_BLKS(sbi))

#define FSCK_START_BLOCK(sbi, segno)	(sbi->SM_I()->main_blkaddr + ((segno) << sbi->log_blocks_per_seg))

#define FSCK_NEXT_FREE_BLKADDR(sbi, curseg)	(FSCK_START_BLOCK(sbi, (curseg)->segno) + (curseg)->next_blkoff)

//#define SIT_BLK_CNT(sbi)						\
//	((MAIN_SEGS(sbi) + SIT_ENTRY_PER_BLOCK - 1) / SIT_ENTRY_PER_BLOCK)

static inline struct fsck_curseg_info *CURSEG_I(fsck_f2fs_sb_info *sbi, int type)
{
	return (struct fsck_curseg_info *)(sbi->SM_I()->curseg_array + type);
}


static inline block_t start_sum_block(fsck_f2fs_sb_info *sbi)
{
	return __start_cp_addr(sbi) + le32_to_cpu(sbi->F2FS_CKPT()->cp_pack_start_sum);
}

static inline block_t sum_blk_addr(fsck_f2fs_sb_info *sbi, int base, int type)
{
	return __start_cp_addr(sbi) + le32_to_cpu(sbi->F2FS_CKPT()->cp_pack_total_block_count)
		- (base + 1) + type;
}

/* for the list of fsync inodes, used only during recovery */
struct fsck_fsync_inode_entry {
	struct list_head list;	/* list head */
	nid_t ino;		/* inode number */
	block_t blkaddr;	/* block address locating the last fsync */
	block_t last_dentry;	/* block address locating the last dentry */
};

//#define nats_in_cursum(jnl)             (le16_to_cpu(jnl->n_nats))
//#define sits_in_cursum(jnl)             (le16_to_cpu(jnl->n_sits))

//#define nat_in_journal(jnl, i)          (jnl->nat_j.entries[i].ne)
//#define nid_in_journal(jnl, i)          (jnl->nat_j.entries[i].nid)
//#define sit_in_journal(jnl, i)          (jnl->sit_j.entries[i].se)
//#define segno_in_journal(jnl, i)        (jnl->sit_j.entries[i].segno)

//#define SIT_ENTRY_OFFSET(sit_i, segno)  ((segno) % sit_i->sents_per_block)
//#define SIT_BLOCK_OFFSET(sit_i, segno)  ((segno) / SIT_ENTRY_PER_BLOCK)
//#define TOTAL_SEGS(sbi)					(sbi->SM_I()->main_segments)

static inline bool IS_VALID_NID(fsck_f2fs_sb_info *sbi, u32 nid)
{
	return (nid < (NAT_ENTRY_PER_BLOCK * le32_to_cpu(sbi->F2FS_RAW_SUPER()->segment_count_nat)
			<< (sbi->log_blocks_per_seg - 1)));
}



static inline bool is_valid_data_blkaddr(block_t blkaddr)
{
	if (blkaddr == NEW_ADDR || blkaddr == NULL_ADDR || blkaddr == COMPRESS_ADDR)
		return 0;
	return 1;
}

static inline int IS_CUR_SEGNO(fsck_f2fs_sb_info *sbi, u32 segno)
{
	int i;

	for (i = 0; i < NO_CHECK_TYPE; i++) {
		struct fsck_curseg_info *curseg = CURSEG_I(sbi, i);
		if (segno == curseg->segno)	return 1;
	}
	return 0;
}

static inline unsigned int BLKOFF_FROM_MAIN(fsck_f2fs_sb_info *sbi, u64 blk_addr)
{
	ASSERT(blk_addr >= sbi->SM_I()->main_blkaddr);
	return (unsigned int)(blk_addr - sbi->SM_I()->main_blkaddr);
}

#undef GET_SEGNO

static inline u32 GET_SEGNO(fsck_f2fs_sb_info *sbi, u64 blk_addr)
{
	return (u32)(BLKOFF_FROM_MAIN(sbi, blk_addr) >> sbi->log_blocks_per_seg);
}

static inline u32 OFFSET_IN_SEG(fsck_f2fs_sb_info *sbi, u64 blk_addr)
{
	return (u32)(BLKOFF_FROM_MAIN(sbi, blk_addr) % (1 << sbi->log_blocks_per_seg));
}
#if 0

static inline void node_info_from_raw_nat(struct node_info *ni,
		struct f2fs_nat_entry *raw_nat)
{
	ni->ino = le32_to_cpu(raw_nat->ino);
	ni->blk_addr = le32_to_cpu(raw_nat->block_addr);
	ni->version = raw_nat->version;
}

static inline void set_summary(struct f2fs_summary *sum, nid_t nid,
			unsigned int ofs_in_node, unsigned char version)
{
	sum->nid = cpu_to_le32(nid);
	sum->_u._s.ofs_in_node = cpu_to_le16(ofs_in_node);
	sum->version = version;
}
#endif


#define S_SHIFT 12
//static unsigned char f2fs_type_by_mode[S_IFMT >> S_SHIFT] = {
//	[S_IFREG >> S_SHIFT]    = F2FS_FT_REG_FILE,
//	[S_IFDIR >> S_SHIFT]    = F2FS_FT_DIR,
//	[S_IFCHR >> S_SHIFT]    = F2FS_FT_CHRDEV,
//	[S_IFBLK >> S_SHIFT]    = F2FS_FT_BLKDEV,
//	[S_IFIFO >> S_SHIFT]    = F2FS_FT_FIFO,
//	[S_IFSOCK >> S_SHIFT]   = F2FS_FT_SOCK,
//	[S_IFLNK >> S_SHIFT]    = F2FS_FT_SYMLINK,
//};

static unsigned char f2fs_type_by_mode[] = {
	F2FS_FT_REG_FILE,
	F2FS_FT_DIR,
	F2FS_FT_CHRDEV,
	F2FS_FT_BLKDEV,
	F2FS_FT_FIFO,
	F2FS_FT_SOCK,
	F2FS_FT_SYMLINK,
};

inline int get_inline_xattr_addrs(f2fs_inode* iinode)
{
	//return (node->_u._s.i_inline_xattr_size);
	if (c.feature & cpu_to_le32(F2FS_FEATURE_FLEXIBLE_INLINE_XATTR))
		return le16_to_cpu(iinode->_u._s.i_inline_xattr_size);
	else if (iinode->i_inline & F2FS_INLINE_XATTR || iinode->i_inline & F2FS_INLINE_DENTRY)
		return DEFAULT_INLINE_XATTR_ADDRS;
	else			return 0;
}

static inline int map_de_type(umode_t mode)
{
       return f2fs_type_by_mode[(mode & S_IFMT) >> S_SHIFT];
}

static inline void *inline_xattr_addr(struct f2fs_inode *inode)
{
	return (void *)&(inode->_u.i_addr[DEF_ADDRS_PER_INODE -	get_inline_xattr_addrs(inode)]);
}

static inline int inline_xattr_size(struct f2fs_inode *inode)
{
	return get_inline_xattr_addrs(inode) * sizeof(__le32);
}

extern int lookup_nat_in_journal(fsck_f2fs_sb_info *sbi, u32 nid, struct f2fs_nat_entry *ne);
#define IS_SUM_NODE_SEG(footer)		(footer.entry_type == SUM_TYPE_NODE)
#define IS_SUM_DATA_SEG(footer)		(footer.entry_type == SUM_TYPE_DATA)

static inline unsigned int dir_buckets(unsigned int level, int dir_level)
{
	if (level + dir_level < MAX_DIR_HASH_DEPTH / 2)
		return 1 << (level + dir_level);
	else
		return MAX_DIR_BUCKETS;
}

static inline unsigned int bucket_blocks(unsigned int level)
{
	if (level < MAX_DIR_HASH_DEPTH / 2)
		return 2;
	else
		return 4;
}

static inline unsigned long dir_block_index(unsigned int level,
				int dir_level, unsigned int idx)
{
	unsigned long i;
	unsigned long bidx = 0;

	for (i = 0; i < level; i++)
		bidx += dir_buckets(i, dir_level) * bucket_blocks(i);
	bidx += idx * bucket_blocks(level);
	return bidx;
}

static inline int is_dot_dotdot(const unsigned char *name, const int len)
{
	if (len == 1 && name[0] == '.')
		return 1;
	if (len == 2 && name[0] == '.' && name[1] == '.')
		return 1;
	return 0;
}

static inline int get_encoding(fsck_f2fs_sb_info *sbi)
{
	return le16_to_cpu(sbi->F2FS_RAW_SUPER()->s_encoding);
}


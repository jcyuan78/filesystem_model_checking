/**
 * fsck.h
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License version 2 as published by the Free Software Foundation. */
#ifndef _FSCK_H_
#define _FSCK_H_

#include <include/f2fs.h>
#include <include/f2fs-filesystem.h>
#include <source/f2fs/segment.h>
#include <source/f2fs/node.h>

#include "./fsck_f2fs.h"

extern struct f2fs_configuration c;

struct quota_ctx;

#define FSCK_UNMATCHED_EXTENT		0x00000001
#define FSCK_INLINE_INODE		0x00000002

enum {
	PREEN_MODE_0,
	PREEN_MODE_1,
	PREEN_MODE_2,
	PREEN_MODE_MAX
};

enum {
	NOERROR_,
	EWRONG_OPT,
	ENEED_ARG,
	EUNKNOWN_OPT,
	EUNKNOWN_ARG,
};

enum SB_ADDR {
	SB0_ADDR = 0,
	SB1_ADDR,
	SB_MAX_ADDR,
};

#define SB_MASK(i)	(1 << i)
#define SB_MASK_ALL	(SB_MASK(SB0_ADDR) | SB_MASK(SB1_ADDR))

/* fsck.c */
struct orphan_info {
	u32 nr_inodes;
	u32 *ino_list;
};

//struct extent_info {
//	u32 fofs;		/* start offset in a file */
//	u32 blk;		/* start block address of the extent */
//	u32 len;		/* length of the extent */
//};

struct child_info {
	u32 state;
	u32 links;
	u32 files;
	u32 pgofs;
	u8 dots;
	u8 dir_level;
	u32 p_ino;		/* parent ino */
	char p_name[F2FS_NAME_LEN + 1]; /* parent name */
	u32 pp_ino;		/* parent parent ino*/
	struct extent_info ei;
	u32 last_blk;
	u32 i_namelen;  /* dentry namelen */
};

struct f2fs_dentry {
	char name[F2FS_NAME_LEN + 1];
	int depth;
	struct f2fs_dentry *next;
};

struct f2fs_fsck {
	f2fs_fsck(CF2fsFileSystem* fs)  {
		memset(this, 0, sizeof(f2fs_fsck));
		sbi.m_fs = fs;
	}
	fsck_f2fs_sb_info sbi;
	struct orphan_info orphani;
	struct chk_result {
		u64 valid_blk_cnt;
		u32 valid_nat_entry_cnt;
		u32 valid_node_cnt;
		u32 valid_inode_cnt;
		u32 multi_hard_link_files;
		u64 sit_valid_blocks;
		u32 sit_free_segs;
		u32 wp_fixed;
		u32 wp_inconsistent_zones;
	} chk;

	struct hard_link_node *hard_link_list_head;

	char *main_seg_usage;
	char *main_area_bitmap;
	char *nat_area_bitmap;
	char *sit_area_bitmap;

	u64 main_area_bitmap_sz;
	u32 nat_area_bitmap_sz;
	u32 sit_area_bitmap_sz;

	u64 nr_main_blks;
	u32 nr_nat_entries;

	u32 dentry_depth;
	struct f2fs_dentry *dentry;
	struct f2fs_dentry *dentry_end;
	struct f2fs_nat_entry *entries;
	u32 nat_valid_inode_cnt;

	struct quota_ctx *qctx;
};

#define BLOCK_SZ		4096
struct block {
	unsigned char buf[BLOCK_SZ];
};

enum NODE_TYPE {
	TYPE_INODE = 37,
	TYPE_DIRECT_NODE = 43,
	TYPE_INDIRECT_NODE = 53,
	TYPE_DOUBLE_INDIRECT_NODE = 67,
	TYPE_XATTR = 77
};

struct hard_link_node {
	u32 nid;
	u32 links;
	u32 actual_links;
	struct hard_link_node *next;
};

enum seg_type {
	SEG_TYPE_DATA,
	SEG_TYPE_CUR_DATA,
	SEG_TYPE_NODE,
	SEG_TYPE_CUR_NODE,
	SEG_TYPE_MAX,
};


struct selabel_handle;

static inline bool need_fsync_data_record(fsck_f2fs_sb_info *sbi)
{
	return !is_set_ckpt_flags(sbi->F2FS_CKPT(), CP_UMOUNT_FLAG) || c.zoned_model == F2FS_ZONED_HM;
}

extern int fsck_chk_orphan_node(fsck_f2fs_sb_info *);
//extern int fsck_chk_quota_node(fsck_f2fs_sb_info *);
//extern int fsck_chk_quota_files(fsck_f2fs_sb_info *);
extern int fsck_sanity_check_nid(fsck_f2fs_sb_info *, u32, f2fs_node *, enum FILE_TYPE, enum NODE_TYPE, struct node_info *);
extern int fsck_chk_node_blk(fsck_f2fs_sb_info *, f2fs_inode *, u32, enum FILE_TYPE, enum NODE_TYPE, u32 *, child_info *);
extern void fsck_chk_inode_blk(fsck_f2fs_sb_info *, u32, enum FILE_TYPE, f2fs_node *, u32 *, struct node_info *, struct child_info *);
extern int fsck_chk_dnode_blk(fsck_f2fs_sb_info *, f2fs_inode *, u32, enum FILE_TYPE, f2fs_node *, u32 *, struct child_info *, struct node_info *);
extern int fsck_chk_idnode_blk(fsck_f2fs_sb_info *, f2fs_inode *,	enum FILE_TYPE, f2fs_node *, u32 *, struct child_info *);
extern int fsck_chk_didnode_blk(fsck_f2fs_sb_info *, f2fs_inode *, enum FILE_TYPE, f2fs_node *, u32 *, struct child_info *);
extern int fsck_chk_data_blk(fsck_f2fs_sb_info *, int, u32, struct child_info *, int, enum FILE_TYPE, u32, u16, u8, int);
extern int fsck_chk_dentry_blk(fsck_f2fs_sb_info *, int,	u32, struct child_info *, int, int);
int fsck_chk_inline_dentries(fsck_f2fs_sb_info *, f2fs_node *, struct child_info *);
void fsck_chk_checkpoint(fsck_f2fs_sb_info *sbi);
int fsck_chk_meta(fsck_f2fs_sb_info *sbi);
void fsck_chk_and_fix_write_pointers(fsck_f2fs_sb_info *);
int fsck_chk_curseg_info(fsck_f2fs_sb_info *);
void pretty_print_filename(const u8 *raw_name, u32 len, char out[F2FS_PRINT_NAMELEN], int enc_name);

extern void update_free_segments(fsck_f2fs_sb_info *);
void print_cp_state(u32);
extern void print_node_info(fsck_f2fs_sb_info *, f2fs_node *, int);
extern void print_inode_info(fsck_f2fs_sb_info *, f2fs_node *, int);
extern fsck_seg_entry *get_seg_entry(fsck_f2fs_sb_info *, unsigned int);
extern struct f2fs_summary_block *get_sum_block(fsck_f2fs_sb_info *, unsigned int, int *);
extern int get_sum_entry(fsck_f2fs_sb_info *, u32, struct f2fs_summary *);
extern void update_sum_entry(fsck_f2fs_sb_info *, block_t, struct f2fs_summary *);
extern void get_node_info(fsck_f2fs_sb_info *, nid_t, struct node_info *);
extern void nullify_nat_entry(fsck_f2fs_sb_info *, u32);
extern void rewrite_sit_area_bitmap(fsck_f2fs_sb_info *);
extern void build_nat_area_bitmap(fsck_f2fs_sb_info *);
extern void build_sit_area_bitmap(fsck_f2fs_sb_info *);
extern int f2fs_set_main_bitmap(fsck_f2fs_sb_info *, u32, int);
extern int f2fs_set_sit_bitmap(fsck_f2fs_sb_info *, u32);
extern void fsck_init(fsck_f2fs_sb_info *);
extern int fsck_verify(fsck_f2fs_sb_info *);
extern void fsck_free(fsck_f2fs_sb_info *);
extern int f2fs_ra_meta_pages(fsck_f2fs_sb_info *, block_t, int, int);
extern int f2fs_do_mount(fsck_f2fs_sb_info *);
extern void f2fs_do_umount(fsck_f2fs_sb_info *);
extern int f2fs_sparse_initialize_meta(fsck_f2fs_sb_info *);

extern void flush_journal_entries(fsck_f2fs_sb_info *);
extern void update_curseg_info(fsck_f2fs_sb_info *, int);
extern void zero_journal_entries(fsck_f2fs_sb_info *);
extern void flush_sit_entries(fsck_f2fs_sb_info *);
extern void move_curseg_info(fsck_f2fs_sb_info *, u64, int);
extern void write_curseg_info(fsck_f2fs_sb_info *);
extern int find_next_free_block(fsck_f2fs_sb_info *, u64 *, int, int, bool);
extern void duplicate_checkpoint(fsck_f2fs_sb_info *);
extern void write_checkpoint(fsck_f2fs_sb_info *);
extern void write_checkpoints(fsck_f2fs_sb_info *);
extern void update_superblock(CF2fsFileSystem * fs, f2fs_super_block *, int);
extern void update_data_blkaddr(fsck_f2fs_sb_info *, nid_t, u16, block_t);
extern void update_nat_blkaddr(fsck_f2fs_sb_info *, nid_t, nid_t, block_t);

extern void print_raw_sb_info(f2fs_super_block *);
extern pgoff_t current_nat_addr(fsck_f2fs_sb_info *, nid_t, int *);

extern u32 get_free_segments(fsck_f2fs_sb_info *);
extern void get_current_sit_page(fsck_f2fs_sb_info *, unsigned int, struct f2fs_sit_block *);
extern void rewrite_current_sit_page(fsck_f2fs_sb_info *, unsigned int, struct f2fs_sit_block *);

extern u32 update_nat_bits_flags(f2fs_super_block *,	f2fs_checkpoint *, u32);
extern void write_nat_bits(fsck_f2fs_sb_info *, f2fs_super_block *, f2fs_checkpoint *, int);
extern unsigned int get_usable_seg_count(fsck_f2fs_sb_info *);
extern bool is_usable_seg(fsck_f2fs_sb_info *, unsigned int);

/* dump.c */
struct dump_option {
	nid_t nid;
	nid_t start_nat;
	nid_t end_nat;
	int start_sit;
	int end_sit;
	int start_ssa;
	int end_ssa;
	int32_t blk_addr;
};

extern void nat_dump(fsck_f2fs_sb_info *, nid_t, nid_t);
extern void sit_dump(fsck_f2fs_sb_info *, unsigned int, unsigned int);
extern void ssa_dump(fsck_f2fs_sb_info *, int, int);
extern int dump_node(fsck_f2fs_sb_info *, nid_t, int);
extern int dump_info_from_blkaddr(fsck_f2fs_sb_info *, u32);
extern unsigned int start_bidx_of_node(unsigned int, f2fs_node *);


/* defrag.c */
int f2fs_defragment(fsck_f2fs_sb_info *, u64, u64, u64, int);

/* resize.c */
int f2fs_resize(fsck_f2fs_sb_info *);

/* sload.c */
int f2fs_sload(fsck_f2fs_sb_info *);

/* segment.c */
int reserve_new_block(fsck_f2fs_sb_info *, block_t *, struct f2fs_summary *, int, bool);
int new_data_block(fsck_f2fs_sb_info *, void *, fsck_dnode_of_data *, int);
int f2fs_build_file(fsck_f2fs_sb_info *, fsck_dentry *);
void f2fs_alloc_nid(fsck_f2fs_sb_info *, nid_t *);
void set_data_blkaddr(fsck_dnode_of_data *);
block_t new_node_block(fsck_f2fs_sb_info *, fsck_dnode_of_data *, unsigned int);

/* segment.c */
struct quota_file;
u64 f2fs_quota_size(CF2fsFileSystem* fs, struct quota_file *);
u64 f2fs_read(fsck_f2fs_sb_info *, nid_t, u8 *, u64, pgoff_t);
enum wr_addr_type {
	WR_NORMAL = 1,
	WR_COMPRESS_DATA = 2,
	WR_NULL_ADDR = NULL_ADDR,		/* 0 */
	WR_NEW_ADDR = NEW_ADDR,			/* -1U */
	WR_COMPRESS_ADDR = COMPRESS_ADDR,	/* -2U */
};
u64 f2fs_write(fsck_f2fs_sb_info *, nid_t, u8 *, u64, pgoff_t);
u64 f2fs_write_compress_data(fsck_f2fs_sb_info *, nid_t, u8 *, u64, pgoff_t);
u64 f2fs_write_addrtag(fsck_f2fs_sb_info *, nid_t, pgoff_t, unsigned int);
void f2fs_filesize_update(fsck_f2fs_sb_info *, nid_t, u64);

int get_dnode_of_data(fsck_f2fs_sb_info *, fsck_dnode_of_data *, pgoff_t, int);
void make_dentry_ptr(struct f2fs_dentry_ptr *, f2fs_node *, void *, int);
int f2fs_create(fsck_f2fs_sb_info *, fsck_dentry *);
int f2fs_mkdir(fsck_f2fs_sb_info *, fsck_dentry *);
int f2fs_symlink(fsck_f2fs_sb_info *, fsck_dentry *);
int inode_set_selinux(fsck_f2fs_sb_info *, u32, const char *);
int f2fs_find_path(fsck_f2fs_sb_info *, char *, nid_t *);
nid_t f2fs_lookup(fsck_f2fs_sb_info *, f2fs_node *, u8 *, int);
int f2fs_add_link(fsck_f2fs_sb_info *, f2fs_node *, const unsigned char *, int, nid_t, int, block_t, int);
struct hardlink_cache_entry *f2fs_search_hardlink(fsck_f2fs_sb_info *sbi, fsck_dentry *de);

/* xattr.c */
void *read_all_xattrs(fsck_f2fs_sb_info *, f2fs_node *);


//#define TOTAL_SEGS(sbi)				(sbi->sm_info ? sbi->sm_info->segment_count : le32_to_cpu(sbi->raw_super->segment_count))

#undef CUR_ADDRS_PER_INODE
inline int CUR_ADDRS_PER_INODE(f2fs_inode* node)
{
	if (f2fs_has_extra_isize(node)) return (DEF_ADDRS_PER_INODE - node->_u._s.i_extra_isize / sizeof(__le32));
	else							return DEF_ADDRS_PER_INODE;
}

unsigned int addrs_per_inode(f2fs_inode* inode);
#if 0
{
	//DEF_ADDRS_PER_INODE - inode->_u._s.i_extra_isize / sizeof(__le32);
	//get_inline_xattr_addrs(inode);
	int default_addr_per_inode = DEF_ADDRS_PER_INODE;
	int cur_addr = CUR_ADDRS_PER_INODE(inode);
	int xattr_addr = get_inline_xattr_addrs(inode);
	LOG_DEBUG(L"default addr=%d, extra size=%d, cur addr=%d, xattr_addr=%d", 
		default_addr_per_inode, inode->_u._s.i_extra_isize, cur_addr, xattr_addr);

	unsigned int addrs = CUR_ADDRS_PER_INODE(inode) - get_inline_xattr_addrs(inode);

	//	unsigned int addrs = (DEF_ADDRS_PER_INODE - inode->_u._s.i_extra_isize / sizeof(__le32)) - get_inline_xattr_addrs(inode);
	return addrs;
	//	if (!f2fs_compressed_file(inode))		
	//	return ALIGN_DOWN(addrs, F2FS_I(inode)->i_cluster_size);
}
#endif


inline size_t MAX_INLINE_DATA(f2fs_inode* iinode)
{
	int extra_isize = iinode->_u._s.i_extra_isize;
	wprintf_s(L"extra_isize=%d, cur_addrs_per_inode=%d, inline_xattr_addrs=%d\n",
		extra_isize, CUR_ADDRS_PER_INODE(iinode), get_inline_xattr_addrs(iinode));

	return (sizeof(__le32) * (CUR_ADDRS_PER_INODE(iinode) - get_inline_xattr_addrs(iinode) - DEF_INLINE_RESERVED_SIZE));
}

static inline bool IS_VALID_BLK_ADDR(fsck_f2fs_sb_info* sbi, u32 addr)
{
	if (addr == NULL_ADDR || addr == NEW_ADDR)	return 1;

	if (addr >= le64_to_cpu(sbi->F2FS_RAW_SUPER()->block_count) || addr < sbi->SM_I()->main_blkaddr) {
		DBG(1, "block addr [0x%x], blk_cnt=%lldd, main_blkaddr=%d\n", addr, 
			sbi->raw_super->block_count, sbi->sm_info->main_blkaddr);
		return 0;
	}
	/* next block offset will be checked at the end of fsck. */
	return 1;
}

int inline test_bit_le(unsigned int nr, const char* addr)
{
	return ((1 << (nr & 7)) & (addr[nr >> 3]));
}

int inline test_and_set_bit_le(unsigned int nr, char* addr)
{
	int mask, retval;

	addr += nr >> 3;
	mask = 1 << ((nr & 0x07));
	retval = mask & *addr;
	*addr |= mask;
	return retval;
}

#endif /* _FSCK_H_ */

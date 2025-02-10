/**
 * node.h
 *
 * Many parts of codes are copied from Linux kernel/fs/f2fs.
 *
 * Copyright (C) 2015 Huawei Ltd.
 * Witten by:
 *   Hou Pengyang <houpengyang@huawei.com>
 *   Liu Shuoran <liushuoran@huawei.com>
 *   Jaegeuk Kim <jaegeuk@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _NODE_H_
#define _NODE_H_

#include "fsck.h"

static inline int IS_INODE(f2fs_node *node)
{
	return ((node)->footer.nid == (node)->footer.ino);
}

#undef ADDRS_PER_PAGE




static inline unsigned int ADDRS_PER_PAGE(fsck_f2fs_sb_info *sbi, f2fs_node *node_blk, f2fs_node *inode_blk)
{
	CF2fsFileSystem* fs = sbi->GetFs();
	nid_t ino = le32_to_cpu(node_blk->footer.ino);
	unsigned int nblocks;

	if (IS_INODE(node_blk))	return addrs_per_inode(&node_blk->i);

	if (!inode_blk) {
		struct node_info ni;

		inode_blk = (f2fs_node*)calloc(BLOCK_SZ, 2);
		ASSERT(inode_blk);

		get_node_info(sbi, ino, &ni);
		ASSERT(fs->dev_read_block(inode_blk, ni.blk_addr) >= 0);
		nblocks = ADDRS_PER_BLOCK(&inode_blk->i);
		free(inode_blk);
	} else {
		nblocks = ADDRS_PER_BLOCK(&inode_blk->i);
	}
	return nblocks;
}

static inline __le32 *blkaddr_in_inode(f2fs_node *node)
{
	return node->i._u.i_addr + get_extra_isize(&node->i);
}

//static inline __le32 *blkaddr_in_node(f2fs_node *node)
//{
//	return IS_INODE(node) ? blkaddr_in_inode(node) : node->dn.addr;
//}

static inline block_t datablock_addr(f2fs_node *node_page, unsigned int offset)
{
	__le32 *addr_array;

	ASSERT(node_page);
	addr_array = blkaddr_in_node(node_page);
	return le32_to_cpu(addr_array[offset]);
}

static inline void set_nid(f2fs_node * rn, int off, nid_t nid, int i)
{
	if (i)
		rn->i.i_nid[off - NODE_DIR1_BLOCK] = cpu_to_le32(nid);
	else
		rn->in.nid[off] = cpu_to_le32(nid);
}

static inline nid_t get_nid(f2fs_node * rn, int off, int i)
{
	if (i)
		return le32_to_cpu(rn->i.i_nid[off - NODE_DIR1_BLOCK]);
	else
		return le32_to_cpu(rn->in.nid[off]);
}

//enum {
//	ALLOC_NODE,	/* allocate a new node page if needed */
//	LOOKUP_NODE,	/* lookup up a node without readahead */
//	LOOKUP_NODE_RA,
//};

static inline void set_new_dnode(fsck_dnode_of_data *dn, f2fs_node *iblk, f2fs_node *nblk, nid_t nid)
{
	memset(dn, 0, sizeof(*dn));
	dn->inode_blk = iblk;
	dn->node_blk = nblk;
	dn->nid = nid;
	dn->idirty = 0;
	dn->ndirty = 0;
}

static inline void inc_inode_blocks(fsck_dnode_of_data *dn)
{
	u64 blocks = le64_to_cpu(dn->inode_blk->i.i_blocks);

	dn->inode_blk->i.i_blocks = cpu_to_le64(blocks + 1);
	dn->idirty = 1;
}

static inline int IS_DNODE(f2fs_node *node_page)
{
	unsigned int ofs = ofs_of_node(node_page);

	if (ofs == 3 || ofs == 4 + NIDS_PER_BLOCK ||
			ofs == 5 + 2 * NIDS_PER_BLOCK)
		return 0;

	if (ofs >= 6 + 2 * NIDS_PER_BLOCK) {
		ofs -= 6 + 2 * NIDS_PER_BLOCK;
		if (!((long int)ofs % (NIDS_PER_BLOCK + 1)))
			return 0;
	}
	return 1;
}

static inline nid_t ino_of_node(f2fs_node *node_blk)
{
	return le32_to_cpu(node_blk->footer.ino);
}

static inline __u64 cpver_of_node(f2fs_node *node_blk)
{
	return le64_to_cpu(node_blk->footer.cp_ver);
}

static inline bool is_recoverable_dnode(fsck_f2fs_sb_info *sbi, f2fs_node *node_blk)
{
	f2fs_checkpoint *ckpt = sbi->F2FS_CKPT();
	__u64 cp_ver = cur_cp_version(ckpt);

	/* Don't care crc part, if fsck.f2fs sets it. */
	if (is_set_ckpt_flags(ckpt, CP_NOCRC_RECOVERY_FLAG))
		return (cp_ver << 32) == (cpver_of_node(node_blk) << 32);

	if (is_set_ckpt_flags(ckpt, CP_CRC_RECOVERY_FLAG))
		cp_ver |= (cur_cp_crc(ckpt) << 32);

	return cp_ver == cpver_of_node(node_blk);
}

static inline block_t next_blkaddr_of_node(f2fs_node *node_blk)
{
	return le32_to_cpu(node_blk->footer.next_blkaddr);
}

static inline int is_node(f2fs_node *node_blk, int type)
{
	return le32_to_cpu(node_blk->footer.flag) & (1 << type);
}

static inline void set_cold_node(f2fs_node *rn, bool is_dir)
{
	unsigned int flag = le32_to_cpu(rn->footer.flag);

	if (is_dir)
		flag &= ~(0x1 << COLD_BIT_SHIFT);
	else
		flag |= (0x1 << COLD_BIT_SHIFT);
	rn->footer.flag = cpu_to_le32(flag);
}

#define is_fsync_dnode(node_blk)	is_node(node_blk, FSYNC_BIT_SHIFT)
#define is_dent_dnode(node_blk)		is_node(node_blk, DENT_BIT_SHIFT)

#endif

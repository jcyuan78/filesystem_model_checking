///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"
// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/inode.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */

//#include <linux/fs.h>
//#include <linux/f2fs_fs.h>
//#include <linux/buffer_head.h>
//#include <linux/backing-dev.h>
//#include <linux/writeback.h>
//
#include "../../include/f2fs.h"
#include "node.h"
#include "segment.h"
#include "xattr.h"

#include "../../include/f2fs-filesystem.h"

#include "../mapping.h"
//
//#include <trace/events/f2fs.h>

LOCAL_LOGGER_ENABLE(L"f2fs.inode", LOGGER_LEVEL_DEBUGINFO);

LOG_CLASS_SIZE(f2fs_inode);

#define DOTIME


bool is_dentry_equal(dentry* src, dentry* dst)
{
	if (IS_ROOT(src) && IS_ROOT(dst)) return true;
	else if (src->d_parent == dst->d_parent)
	{
		if (src->d_name.name == dst->d_name.name) return true;
	}
	return false;
}

dentry* f2fs_inode_info::splice_alias(dentry* entry)
{
	EnterCriticalSection(&m_alias_lock);
	for (auto it = m_alias.begin(); it != m_alias.end(); ++it)
	{
		if (is_dentry_equal(*it, entry))
		{
			LeaveCriticalSection(&m_alias_lock);
			return dget(*it);
		}
	}
	m_alias.push_back(entry);
	entry->d_inode = this;
	LeaveCriticalSection(&m_alias_lock);
	return dget(entry);
}

void f2fs_inode_info::remove_alias(dentry* ddentry)
{
	EnterCriticalSection(&m_alias_lock);
	m_alias.remove(ddentry);
	LeaveCriticalSection(&m_alias_lock);
}

//void f2fs_mark_inode_dirty_sync(inode *inode, bool sync)
void f2fs_inode_info::f2fs_mark_inode_dirty_sync(bool sync)
{
	if (is_inode_flag_set(FI_NEW_INODE))	return;
	if (f2fs_inode_dirtied(sync))		return;
	mark_inode_dirty_sync();
}


void f2fs_set_inode_flags(inode *inode)
{
	unsigned int flags = F2FS_I(inode)->i_flags;
	unsigned int new_fl = 0;

	if (flags & F2FS_SYNC_FL)			new_fl |= S_SYNC;
	if (flags & F2FS_APPEND_FL)			new_fl |= S_APPEND;
	if (flags & F2FS_IMMUTABLE_FL)		new_fl |= S_IMMUTABLE;
	if (flags & F2FS_NOATIME_FL)		new_fl |= S_NOATIME;
	if (flags & F2FS_DIRSYNC_FL)		new_fl |= S_DIRSYNC;
	if (file_is_encrypt(inode))			new_fl |= S_ENCRYPTED;
	if (file_is_verity(inode))			new_fl |= S_VERITY;
	if (flags & F2FS_CASEFOLD_FL)		new_fl |= S_CASEFOLD;
	inode_set_flags(inode, new_fl, S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC| S_ENCRYPTED|S_VERITY|S_CASEFOLD);
}


#define MINORBITS	20
#define MINORMASK	((1U << MINORBITS) - 1)

#define MAJOR(dev)	((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)	((unsigned int) ((dev) & MINORMASK))
#define MKDEV(ma,mi)	(((ma) << MINORBITS) | (mi))

static /*__always_inline*/ u16 old_encode_dev(dev_t dev)
{
	return (MAJOR(dev) << 8) | MINOR(dev);
}

static /*__always_inline*/ dev_t old_decode_dev(u16 val)
{
	return MKDEV((val >> 8) & 255, val & 255);
}

static/* __always_inline*/ u32 new_encode_dev(dev_t dev)
{
	unsigned major = MAJOR(dev);
	unsigned minor = MINOR(dev);
	return (minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12);
}

static /*__always_inline*/ dev_t new_decode_dev(u32 dev)
{
	unsigned major = (dev & 0xfff00) >> 8;
	unsigned minor = (dev & 0xff) | ((dev >> 12) & 0xfff00);
	return MKDEV(major, minor);
}

static void __get_inode_rdev(inode *inode, f2fs_inode *ri)
{
	int extra_size = F2FS_I(inode)->get_extra_isize();

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) ||	S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) 
	{
		if (ri->_u.i_addr[extra_size])
			inode->i_rdev = old_decode_dev(	le32_to_cpu(ri->_u.i_addr[extra_size]));
		else
			inode->i_rdev = new_decode_dev(	le32_to_cpu(ri->_u.i_addr[extra_size + 1]));
	}
}


static int __written_first_block(f2fs_sb_info *sbi, f2fs_inode *ri)
{
	block_t addr = le32_to_cpu(ri->_u.i_addr[offset_in_addr(ri)]);

	if (!__is_valid_data_blkaddr(addr))		return 1;
	if (!sbi->f2fs_is_valid_blkaddr(addr, DATA_GENERIC_ENHANCE))		return -EFSCORRUPTED;
	return 0;
}

//static void __set_inode_rdev(struct inode *inode, struct f2fs_inode *ri)
void f2fs_inode_info::__set_inode_rdev( f2fs_inode* ri)
{
	int extra_size = get_extra_isize();

	if (S_ISCHR(i_mode) || S_ISBLK(i_mode)) 
	{
//		if (old_valid_dev(i_rdev)) 
		if (MAJOR(i_rdev) < 256 && MINOR(i_rdev) < 256)
		{
			ri->_u.i_addr[extra_size] = cpu_to_le32(old_encode_dev(i_rdev));
			ri->_u.i_addr[extra_size + 1] = 0;
		} 
		else 
		{
			ri->_u.i_addr[extra_size] = 0;
			ri->_u.i_addr[extra_size + 1] = cpu_to_le32(new_encode_dev(i_rdev));
			ri->_u.i_addr[extra_size + 2] = 0;
		}
	}
}

static void __recover_inline_status(struct inode *inode, struct page *ipage)
{
	f2fs_inode_info* fi = F2FS_I(inode);
	void *inline_data = fi->inline_data_addr(ipage);
	__le32 *start = (__le32*)inline_data;
	__le32 *end = start + MAX_INLINE_DATA(fi) / sizeof(__le32);

	while (start < end)
	{
		if (*start++) 
		{
			f2fs_wait_on_page_writeback(ipage, NODE, true, true);

			set_inode_flag(fi, FI_DATA_EXIST);
			set_raw_inline(inode, F2FS_INODE(ipage));
			set_page_dirty(ipage);
			return;
		}
	}
	return;
}

static bool f2fs_enable_inode_chksum(f2fs_sb_info *sbi, struct page *page)
{
	f2fs_inode *ri = &F2FS_NODE(page)->i;

	if (!f2fs_sb_has_inode_chksum(sbi))
		return false;

	if (!IS_INODE(page) || !(ri->i_inline & F2FS_EXTRA_ATTR))
		return false;

	if (!F2FS_FITS_IN_INODE(ri, le16_to_cpu(ri->_u._s.i_extra_isize),	_u._s.i_inode_checksum))
		return false;

	return true;
}

//static __u32 f2fs_inode_chksum(struct f2fs_sb_info *sbi, struct page *page)
//{
//	struct f2fs_node *node = F2FS_NODE(page);
//	struct f2fs_inode *ri = &node->i;
//	__le32 ino = node->footer.ino;
//	__le32 gen = ri->i_generation;
//	__u32 chksum, chksum_seed;
//	__u32 dummy_cs = 0;
//	unsigned int offset = offsetof(struct f2fs_inode, _u._s.i_inode_checksum);
//	unsigned int cs_size = sizeof(dummy_cs);
//
//	chksum = f2fs_chksum(sbi, sbi->s_chksum_seed, (__u8 *)&ino,					sizeof(ino));
//	chksum_seed = f2fs_chksum(sbi, chksum, (__u8 *)&gen, sizeof(gen));
//
//	chksum = f2fs_chksum(sbi, chksum_seed, (__u8 *)ri, offset);
//	chksum = f2fs_chksum(sbi, chksum, (__u8 *)&dummy_cs, cs_size);
//	offset += cs_size;
//	chksum = f2fs_chksum(sbi, chksum, (__u8 *)ri + offset,			F2FS_BLKSIZE - offset);
//	return chksum;
//}

bool f2fs_inode_chksum_verify(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_inode *ri;
	__u32 provided, calculated;

	if (unlikely(sbi->is_sbi_flag_set( SBI_IS_SHUTDOWN)))	return true;

#ifdef CONFIG_F2FS_CHECK_FS
	if (!f2fs_enable_inode_chksum(sbi, page))	return true;
#else
	if (!f2fs_enable_inode_chksum(sbi, page) || PageDirty(page) || PageWriteback(page)) return true;
#endif

	ri = &F2FS_NODE(page)->i;
	provided = le32_to_cpu(ri->_u._s.i_inode_checksum);
	calculated = sbi->m_fs->f2fs_inode_chksum(page);

	if (provided != calculated)
		f2fs_warn(sbi, L"checksum invalid, nid = %lu, ino_of_node = %x, %x vs. %x",
			  page->index, ino_of_node(page), provided, calculated);

	return provided == calculated;
}

void f2fs_inode_chksum_set(f2fs_sb_info *sbi, page *pp)
{
	f2fs_inode *ri = &F2FS_NODE(pp)->i;
	if (!f2fs_enable_inode_chksum(sbi, pp))	return;
	ri->_u._s.i_inode_checksum = cpu_to_le32(sbi->m_fs->f2fs_inode_chksum(pp));
}

static bool sanity_check_inode(struct inode *inode, struct page *node_page)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	f2fs_inode_info *fi = F2FS_I(inode);
	struct f2fs_inode *ri = F2FS_INODE(node_page);
	unsigned long long iblocks;

	iblocks = le64_to_cpu(F2FS_INODE(node_page)->i_blocks);
	if (!iblocks)
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		f2fs_warn(sbi, L"%s: corrupted inode i_blocks i_ino=%lx iblocks=%llu, run fsck to fix.", __func__, 
			inode->i_ino, iblocks);
		return false;
	}

	if (ino_of_node(node_page) != nid_of_node(node_page)) 
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		f2fs_warn(sbi, L"%s: corrupted inode footer i_ino=%lx, ino,nid: [%u, %u] run fsck to fix.",  __func__, 
			inode->i_ino,  ino_of_node(node_page), nid_of_node(node_page));
		return false;
	}

	if (f2fs_sb_has_flexible_inline_xattr(sbi)	&& !f2fs_has_extra_attr(inode)) 
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		f2fs_warn(sbi, L"%s: corrupted inode ino=%lx, run fsck to fix.",  __func__, inode->i_ino);
		return false;
	}

	if (f2fs_has_extra_attr(inode) && !f2fs_sb_has_extra_attr(sbi)) 
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		f2fs_warn(sbi, L"%s: inode (ino=%lx) is with extra_attr, but extra_attr feature is off", __func__, inode->i_ino);
		return false;
	}

	if (fi->i_extra_isize > F2FS_TOTAL_EXTRA_ATTR_SIZE || fi->i_extra_isize % sizeof(__le32))
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		f2fs_warn(sbi, L"%s: inode (ino=%lx) has corrupted i_extra_isize: %d, max: %zu",  __func__, inode->i_ino, 
			fi->i_extra_isize,  F2FS_TOTAL_EXTRA_ATTR_SIZE);
		return false;
	}

	if (f2fs_has_extra_attr(inode) && f2fs_sb_has_flexible_inline_xattr(sbi) && f2fs_has_inline_xattr(inode) &&
		(!fi->i_inline_xattr_size || fi->i_inline_xattr_size > MAX_INLINE_XATTR_SIZE)) 
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		f2fs_warn(sbi, L"%s: inode (ino=%lx) has corrupted i_inline_xattr_size: %d, max: %zu", __func__, inode->i_ino, 
			fi->i_inline_xattr_size, MAX_INLINE_XATTR_SIZE);
		return false;
	}

	if (F2FS_I(inode)->extent_tree) 
	{
		extent_info *ei = &(F2FS_I(inode))->extent_tree->largest;

		if (ei->len && (!sbi->f2fs_is_valid_blkaddr(ei->blk, DATA_GENERIC_ENHANCE) ||
			!sbi->f2fs_is_valid_blkaddr(ei->blk + ei->len - 1, DATA_GENERIC_ENHANCE)))
		{
			sbi->set_sbi_flag(SBI_NEED_FSCK);
			f2fs_warn(sbi, L"%s: inode (ino=%lx) extent info [%u, %u, %u] is incorrect, run fsck to fix", __func__, 
				inode->i_ino,  ei->blk, ei->fofs, ei->len);
			return false;
		}
	}

	if (f2fs_has_inline_data(inode) && (!S_ISREG(inode->i_mode) && !S_ISLNK(inode->i_mode))) 
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		f2fs_warn(sbi, L"%s: inode (ino=%lx, mode=%u) should not have inline_data, run fsck to fix",
			  __func__, inode->i_ino, inode->i_mode);
		return false;
	}

	if (fi->f2fs_has_inline_dentry() && !S_ISDIR(inode->i_mode)) 
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		f2fs_warn(sbi, L"%s: inode (ino=%lx, mode=%u) should not have inline_dentry, run fsck to fix",
			  __func__, inode->i_ino, inode->i_mode);
		return false;
	}

	if ((fi->i_flags & F2FS_CASEFOLD_FL) && !f2fs_sb_has_casefold(sbi)) 
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		f2fs_warn(sbi, L"%s: inode (ino=%lx) has casefold flag, but casefold feature is off",
			  __func__, inode->i_ino);
		return false;
	}

	if (f2fs_has_extra_attr(inode) && f2fs_sb_has_compression(sbi) && fi->i_flags & F2FS_COMPR_FL &&
			F2FS_FITS_IN_INODE(ri, fi->i_extra_isize, _u._s.i_log_cluster_size))
	{
#if 0 //TODO
		if (ri->_u._s.i_compress_algorithm >= COMPRESS_MAX) 
		{
			sbi->set_sbi_flag(SBI_NEED_FSCK);
			f2fs_warn(sbi, L"%s: inode (ino=%lx) has unsupported compress algorithm: %u, run fsck to fix",
				  __func__, inode->i_ino, ri->_u._s.i_compress_algorithm);
			return false;
		}
#endif //TODO
		if (le64_to_cpu(ri->_u._s.i_compr_blocks) >	SECTOR_TO_BLOCK(inode->i_blocks)) 
		{
			sbi->set_sbi_flag(SBI_NEED_FSCK);
			f2fs_warn(sbi, L"%s: inode (ino=%lx) has inconsistent i_compr_blocks:%llu, i_blocks:%llu, run fsck to fix",
				  __func__, inode->i_ino, le64_to_cpu(ri->_u._s.i_compr_blocks), SECTOR_TO_BLOCK(inode->i_blocks));
			return false;
		}
		if (ri->_u._s.i_log_cluster_size < MIN_COMPRESS_LOG_SIZE || ri->_u._s.i_log_cluster_size > MAX_COMPRESS_LOG_SIZE) 
		{
			sbi->set_sbi_flag(SBI_NEED_FSCK);
			f2fs_warn(sbi, L"%s: inode (ino=%lx) has unsupported log cluster size: %u, run fsck to fix",
				  __func__, inode->i_ino, ri->_u._s.i_log_cluster_size);
			return false;
		}
	}
	return true;
}

#if 0  //<OBSOLETE>
int CF2fsFileSystem::do_read_inode(inode *ptr_inode)
{
	//<YUAN>根据inode指向的
//	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	f2fs_sb_info* sbi = m_sb_info;
	f2fs_inode_info *fi = F2FS_I(ptr_inode);
	page *node_page;
	f2fs_inode *ri;
	projid_t projid;
	int err;

	/* Check if ino is within scope */
	if (f2fs_check_nid_range(sbi, ptr_inode->i_ino))	return -EINVAL;

	// 从inode获取相关page => 获取f2fs_inode_inf，可能在此函数中，实际读取inode
	node_page = sbi->f2fs_get_node_page(ptr_inode->i_ino);
	if (IS_ERR(node_page))	return (int)PTR_ERR(node_page);
	// node_page的地址及f2fs_inode（raw_inode)
	ri = F2FS_INODE(node_page);

	ptr_inode->i_mode = le16_to_cpu(ri->i_mode);
	//i_uid_write(ptr_inode, le32_to_cpu(ri->i_uid));
	//i_gid_write(ptr_inode, le32_to_cpu(ri->i_gid));
	ptr_inode->set_nlink(le32_to_cpu(ri->i_links));
	ptr_inode->i_size = le64_to_cpu(ri->i_size);
	ptr_inode->i_blocks = SECTOR_FROM_BLOCK(le64_to_cpu(ri->i_blocks) - 1);

#ifdef DOTIME
	ptr_inode->i_atime = le64_to_cpu(ri->i_atime);
	ptr_inode->i_ctime = le64_to_cpu(ri->i_ctime);
	ptr_inode->i_mtime = le64_to_cpu(ri->i_mtime);
	//ptr_inode->i_atime.tv_nsec = le32_to_cpu(ri->i_atime_nsec);
	//ptr_inode->i_ctime.tv_nsec = le32_to_cpu(ri->i_ctime_nsec);
	//ptr_inode->i_mtime.tv_nsec = le32_to_cpu(ri->i_mtime_nsec);
#endif
	ptr_inode->i_generation = le32_to_cpu(ri->i_generation);
	if (S_ISDIR(ptr_inode->i_mode))			fi->i_current_depth = le32_to_cpu(ri->i_current_depth);
	else if (S_ISREG(ptr_inode->i_mode))	fi->i_gc_failures[GC_FAILURE_PIN] =	le16_to_cpu(ri->i_gc_failures);
	fi->i_xattr_nid = le32_to_cpu(ri->i_xattr_nid);
	fi->i_flags = le32_to_cpu(ri->i_flags);
	if (S_ISREG(ptr_inode->i_mode))		fi->i_flags &= ~F2FS_PROJINHERIT_FL;
	bitmap_zero((unsigned long*)(&fi->flags), FI_MAX);
	fi->i_advise = ri->i_advise;
	fi->i_pino = le32_to_cpu(ri->i_pino);
	fi->i_dir_level = ri->i_dir_level;

	f2fs_init_extent_tree(ptr_inode, node_page);

	get_inline_info(ptr_inode, ri);

	fi->i_extra_isize = f2fs_has_extra_attr(ptr_inode) ? le16_to_cpu(ri->_u._s.i_extra_isize) : 0;

	if (f2fs_sb_has_flexible_inline_xattr(sbi)) 
	{
		fi->i_inline_xattr_size = le16_to_cpu(ri->_u._s.i_inline_xattr_size);
	} 
	else if (f2fs_has_inline_xattr(ptr_inode) || fi->f2fs_has_inline_dentry()) 
	{
		fi->i_inline_xattr_size = DEFAULT_INLINE_XATTR_ADDRS;
	} 
	else 
	{	/* Previous inline data or directory always reserved 200 bytes in inode layout, even if inline_xattr is disabled.
		In order to keep inline_dentry's structure for backward compatibility, we get the space back only from nline_data.
		 */
		fi->i_inline_xattr_size = 0;
	}

	if (!sanity_check_inode(ptr_inode, node_page)) 
	{
		f2fs_put_page(node_page, 1);
		return -EFSCORRUPTED;
	}

	/* check data exist */
	if (f2fs_has_inline_data(ptr_inode) && !f2fs_exist_data(ptr_inode))		__recover_inline_status(ptr_inode, node_page);

	/* try to recover cold bit for non-dir inode */
	if (!S_ISDIR(ptr_inode->i_mode) && !is_cold_node(node_page)) 
	{
		f2fs_wait_on_page_writeback(node_page, NODE, true, true);
		set_cold_node(node_page, false);
		set_page_dirty(node_page);
	}

	/* get rdev by using inline_info */
	__get_inode_rdev(ptr_inode, ri);

	if (S_ISREG(ptr_inode->i_mode)) 
	{
		err = __written_first_block(sbi, ri);
		if (err < 0) 
		{
			f2fs_put_page(node_page, 1);
			return err;
		}
		if (!err)	fi->set_inode_flag(FI_FIRST_BLOCK_WRITTEN);
	}

	if (!f2fs_need_inode_block_update(sbi, ptr_inode->i_ino))		fi->last_disk_size = ptr_inode->i_size;

	if (fi->i_flags & F2FS_PROJINHERIT_FL)
		fi->set_inode_flag(FI_PROJ_INHERIT);

	if (f2fs_has_extra_attr(ptr_inode) && f2fs_sb_has_project_quota(sbi) && F2FS_FITS_IN_INODE(ri, fi->i_extra_isize, _u._s.i_projid))
		projid = (projid_t)le32_to_cpu(ri->_u._s.i_projid);
	else	projid = F2FS_DEF_PROJID;
#if 0
	fi->_u._s.i_projid = make_kprojid(&init_user_ns, i_projid);
#endif

	if (f2fs_has_extra_attr(ptr_inode) && f2fs_sb_has_inode_crtime(sbi) && F2FS_FITS_IN_INODE(ri, fi->i_extra_isize, _u._s.i_crtime))
	{
#ifdef DOTIME
		fi->i_crtime = le64_to_cpu(ri->i_ctime);
		//fi->i_crtime.tv_nsec = le32_to_cpu(ri->i_crtime_nsec);
#endif
	}

	if (f2fs_has_extra_attr(ptr_inode) && f2fs_sb_has_compression(sbi) && (fi->i_flags & F2FS_COMPR_FL)) 
	{
#if 0 //COMPRESSION
		if (F2FS_FITS_IN_INODE(ri, fi->i_extra_isize, i_log_cluster_size)) 
		{
			atomic_set(&fi->i_compr_blocks, le64_to_cpu(ri->i_compr_blocks));
			fi->i_compress_algorithm = ri->i_compress_algorithm;
			fi->i_log_cluster_size = ri->i_log_cluster_size;
			fi->i_compress_flag = le16_to_cpu(ri->i_compress_flag);
			fi->i_cluster_size = 1 << fi->i_log_cluster_size;
			set_inode_flag(ptr_inode, FI_COMPRESSED_FILE);
		}
#endif
	}
#ifdef DOTIME
	F2FS_I(ptr_inode)->i_disk_time[0] = ptr_inode->i_atime;
	F2FS_I(ptr_inode)->i_disk_time[1] = ptr_inode->i_ctime;
	F2FS_I(ptr_inode)->i_disk_time[2] = ptr_inode->i_mtime;
	F2FS_I(ptr_inode)->i_disk_time[3] = F2FS_I(ptr_inode)->i_crtime;
#endif
	f2fs_put_page(node_page, 1);

	stat_inc_inline_xattr(ptr_inode);
	stat_inc_inline_inode(ptr_inode);
	stat_inc_inline_dir(ptr_inode);
	stat_inc_compr_inode(ptr_inode);
	stat_add_compr_blocks(ptr_inode, atomic_read(&fi->i_compr_blocks));

	return 0;
}
#endif

//static int do_read_inode(struct inode* inode)


int f2fs_sb_info::do_create_read_inode(f2fs_inode_info*& out_inode, unsigned long ino)
{
//	LOG_STACK_TRACE();
	JCASSERT(out_inode == 0);
	f2fs_inode_info* fi = NULL;
	page* node_page;
	f2fs_inode* ri;
	projid_t projid;
	int err;

	/* Check if ino is within scope */
	if (f2fs_check_nid_range(this, ino))	return -EINVAL;

	// 从inode获取相关page => 获取f2fs_inode_inf，可能在此函数中，实际读取inode
	node_page = f2fs_get_node_page(ino);
	if (IS_ERR(node_page))
	{
		err = (int)PTR_ERR(node_page);
		LOG_ERROR(L"[err] failed on reading inode page, ino=%d, err=%d", ino, err);
		return err;
	}
	// node_page的地址及f2fs_inode（raw_inode)
	ri = F2FS_INODE(node_page);
	umode_t mode = le16_to_cpu(ri->i_mode);

	bool thp_support = s_type->fs_flags & FS_THP_SUPPORT;

	address_space* mapping = NULL;

	if (S_ISREG(mode))
	{
		fi = static_cast <f2fs_inode_info*>(GetInodeLocked<Cf2fsFileNode>(thp_support, ino));
		LOG_DEBUG_(0, L"[inode_track] add=%p, ino=%d, - create file, size lock=%d", fi, ino, fi->i_size_lock.LockCount);
		// mapping在Cf2fsDirInode中处理
		//inode->i_op = &f2fs_file_inode_operations;
		//inode->i_fop = &f2fs_file_operations;
		//inode->i_mapping->a_ops = &f2fs_dblock_aops;
	}
	else if (S_ISDIR(mode))
	{
		fi = static_cast<f2fs_inode_info*>(GetInodeLocked<Cf2fsDirInode>(thp_support, ino));
		LOG_DEBUG_(0, L"[inode_track] add=%p, ino=%d, - mkdir, size lock=%d", fi, ino, fi->i_size_lock.LockCount);

		// mapping在Cf2fsDirInode中处理
	}
	else if (S_ISLNK(mode))
	{
		fi = static_cast<f2fs_inode_info*>(GetInodeLocked<Cf2fsSymbLink>(thp_support, ino));
		//if (file_is_encrypt(inode))			inode->i_op = &f2fs_encrypted_symlink_inode_operations;
		//else								inode->i_op = &f2fs_symlink_inode_operations;
		//inode_nohighmem(ptr_inode);
		//inode->i_mapping->a_ops = &f2fs_dblock_aops;
	}
	else if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode) || S_ISSOCK(mode))
	{
		fi = static_cast<f2fs_inode_info*>(GetInodeLocked<Cf2fsSpecialInode>(thp_support, ino));
		LOG_DEBUG_(0, L"[inode_track] add=%p, ino=%d, - create special, size lock=%d", fi, ino, fi->i_size_lock.LockCount);

		//		JCASSERT(0);
		//inode->i_op = &f2fs_special_inode_operations;
		//init_special_inode(ptr_inode, ptr_inode->i_mode, ptr_inode->i_rdev);
	}
	else
	{
		LOG_ERROR(L"[err] unknown inode mode (%d)", mode);
		return 1;
	}

	f2fs_inode_info* ptr_inode = fi;
	out_inode = ptr_inode;

//	ptr_inode->i_mode = le16_to_cpu(ri->i_mode);
	ptr_inode->i_mode = mode;
	//i_uid_write(ptr_inode, le32_to_cpu(ri->i_uid));
	//i_gid_write(ptr_inode, le32_to_cpu(ri->i_gid));
	ptr_inode->set_nlink(le32_to_cpu(ri->i_links));
	ptr_inode->i_size = le64_to_cpu(ri->i_size);
	ptr_inode->i_blocks = SECTOR_FROM_BLOCK(le64_to_cpu(ri->i_blocks) - 1);
	LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, link=%d - read from disk", ptr_inode, ptr_inode->i_ino, ptr_inode->i_nlink);
#ifdef DOTIME
	ptr_inode->i_atime = le64_to_cpu(ri->i_atime);
	ptr_inode->i_ctime = le64_to_cpu(ri->i_ctime);
	ptr_inode->i_mtime = le64_to_cpu(ri->i_mtime);
	//ptr_inode->i_atime.tv_nsec = le32_to_cpu(ri->i_atime_nsec);
	//ptr_inode->i_ctime.tv_nsec = le32_to_cpu(ri->i_ctime_nsec);
	//ptr_inode->i_mtime.tv_nsec = le32_to_cpu(ri->i_mtime_nsec);
#endif
	ptr_inode->i_generation = le32_to_cpu(ri->i_generation);
	if (S_ISDIR(ptr_inode->i_mode))			fi->i_current_depth = le32_to_cpu(ri->i_current_depth);
	else if (S_ISREG(ptr_inode->i_mode))	fi->i_gc_failures[GC_FAILURE_PIN] = le16_to_cpu(ri->i_gc_failures);
	fi->i_xattr_nid = le32_to_cpu(ri->i_xattr_nid);
	fi->i_flags = le32_to_cpu(ri->i_flags);
	if (S_ISREG(ptr_inode->i_mode))		fi->i_flags &= ~F2FS_PROJINHERIT_FL;
	bitmap_zero((unsigned long*)(&fi->flags), FI_MAX);
	fi->i_advise = ri->i_advise;
	fi->i_pino = le32_to_cpu(ri->i_pino);
	fi->i_dir_level = ri->i_dir_level;

	f2fs_init_extent_tree(ptr_inode, node_page);
	get_inline_info(ptr_inode, ri);

	fi->i_extra_isize = f2fs_has_extra_attr(ptr_inode) ? le16_to_cpu(ri->_u._s.i_extra_isize) : 0;

	if (f2fs_sb_has_flexible_inline_xattr(this))
	{
		fi->i_inline_xattr_size = le16_to_cpu(ri->_u._s.i_inline_xattr_size);
	}
	else if (f2fs_has_inline_xattr(ptr_inode) || ptr_inode->f2fs_has_inline_dentry())
	{
		fi->i_inline_xattr_size = DEFAULT_INLINE_XATTR_ADDRS;
	}
	else
	{	/* Previous inline data or directory always reserved 200 bytes in inode layout, even if inline_xattr is disabled.
		In order to keep inline_dentry's structure for backward compatibility, we get the space back only from nline_data.	 */
		fi->i_inline_xattr_size = 0;
	}

	if (!sanity_check_inode(ptr_inode, node_page))
	{
		f2fs_put_page(node_page, 1);
		return -EFSCORRUPTED;
	}
	/* check data exist */
	if (f2fs_has_inline_data(ptr_inode) && !f2fs_exist_data(ptr_inode))		__recover_inline_status(ptr_inode, node_page);
	/* try to recover cold bit for non-dir inode */
	if (!S_ISDIR(ptr_inode->i_mode) && !is_cold_node(node_page))
	{
		f2fs_wait_on_page_writeback(node_page, NODE, true, true);
		set_cold_node(node_page, false);
		set_page_dirty(node_page);
	}

	/* get rdev by using inline_info */
	__get_inode_rdev(ptr_inode, ri);

	if (S_ISREG(ptr_inode->i_mode))
	{
		err = __written_first_block(this, ri);
		if (err < 0)
		{
			f2fs_put_page(node_page, 1);
			return err;
		}
		if (!err)	set_inode_flag(ptr_inode, FI_FIRST_BLOCK_WRITTEN);
	}

	if (!f2fs_need_inode_block_update(this, ptr_inode->i_ino))		fi->last_disk_size = ptr_inode->i_size;

	if (fi->i_flags & F2FS_PROJINHERIT_FL)
		set_inode_flag(ptr_inode, FI_PROJ_INHERIT);

	if (f2fs_has_extra_attr(ptr_inode) && f2fs_sb_has_project_quota(this) && F2FS_FITS_IN_INODE(ri, fi->i_extra_isize, _u._s.i_projid))
		projid = (projid_t)le32_to_cpu(ri->_u._s.i_projid);
	else	projid = F2FS_DEF_PROJID;
#if 0
	fi->_u._s.i_projid = make_kprojid(&init_user_ns, i_projid);
#endif

	if (f2fs_has_extra_attr(ptr_inode) && f2fs_sb_has_inode_crtime(this) && F2FS_FITS_IN_INODE(ri, fi->i_extra_isize, _u._s.i_crtime))
	{
#ifdef DOTIME
		fi->i_crtime = le64_to_cpu(ri->i_ctime);
		//fi->i_crtime.tv_nsec = le32_to_cpu(ri->i_crtime_nsec);
#endif
	}

	if (f2fs_has_extra_attr(ptr_inode) && f2fs_sb_has_compression(this) && (fi->i_flags & F2FS_COMPR_FL))
	{
#if 0 //COMPRESSION
		if (F2FS_FITS_IN_INODE(ri, fi->i_extra_isize, i_log_cluster_size))
		{
			atomic_set(&fi->i_compr_blocks, le64_to_cpu(ri->i_compr_blocks));
			fi->i_compress_algorithm = ri->i_compress_algorithm;
			fi->i_log_cluster_size = ri->i_log_cluster_size;
			fi->i_compress_flag = le16_to_cpu(ri->i_compress_flag);
			fi->i_cluster_size = 1 << fi->i_log_cluster_size;
			set_inode_flag(ptr_inode, FI_COMPRESSED_FILE);
		}
#endif
	}
#ifdef DOTIME
	ptr_inode->i_disk_time[0] = ptr_inode->i_atime;
	ptr_inode->i_disk_time[1] = ptr_inode->i_ctime;
	ptr_inode->i_disk_time[2] = ptr_inode->i_mtime;
	ptr_inode->i_disk_time[3] = ptr_inode->i_crtime;
#endif
	f2fs_put_page(node_page, 1);
	stat_inc_inline_xattr(ptr_inode);
	stat_inc_inline_inode(ptr_inode);
	stat_inc_inline_dir(ptr_inode);
	stat_inc_compr_inode(ptr_inode);
	stat_add_compr_blocks(ptr_inode, atomic_read(&fi->i_compr_blocks));

	return 0;
}

#if 0
inode * CF2fsFileSystem::f2fs_iget(unsigned long ino)
{
	f2fs_sb_info* sbi = m_sb_info;
	inode *ptr_inode = NULL;
	// 用于虚函数实现，针对不同的类型，创建不同的inode对象
	inode* typed_inode = NULL;
	int ret = 0;
	address_space* mapping = NULL;

	//<YUAN> iget_locked 调用 inode_init_always初始化inode, i_mapping指向i_data
	f2fs_inode_info* fi = new f2fs_inode_info;
	ptr_inode = static_cast<inode*>(fi);
	m_inodes.internal_iget_locked(ptr_inode, (m_sb_info->s_type->fs_flags & FS_THP_SUPPORT), ino);
//	ptr_inode = m_inodes.iget_locked((m_sb_info->s_type->fs_flags & FS_THP_SUPPORT), ino);
//	f2fs_inode_info* fi = F2FS_I(ptr_inode);
//	if (!inode)	return (inode*)(-ENOMEM);
	if (!ptr_inode) THROW_ERROR(ERR_APP, L"failed on locking inodes");
	ptr_inode->i_sb = m_sb_info;

	if (!(ptr_inode->i_state & I_NEW)) 
	{	//<YUAN>无法找到trace_f2fs_iget()
//		trace_f2fs_iget(ptr_inode);
		return ptr_inode;
	}
	if (ino == sbi->F2FS_NODE_INO() || ino == sbi->F2FS_META_INO())		goto make_now;

	ret = do_read_inode(ptr_inode);
	if (ret)		goto bad_inode;

make_now:
	if (ino == sbi->F2FS_NODE_INO()) 
	{
//		inode->i_mapping->a_ops = &f2fs_node_aops;
		//mapping_set_gfp_mask(inode->i_mapping, GFP_NOFS);
		mapping = new Cf2fsNodeMapping;
		typed_inode = ptr_inode;
	}
	else if (ino == sbi->F2FS_META_INO()) 
	{
//		inode->i_mapping->a_ops = &f2fs_meta_aops;
		//mapping_set_gfp_mask(inode->i_mapping, GFP_NOFS);
		mapping = new Cf2fsMetaMapping;
		typed_inode = ptr_inode;
	} 
	else if (S_ISREG(ptr_inode->i_mode)) 
	{
		JCASSERT(0);
		//inode->i_op = &f2fs_file_inode_operations;
		//inode->i_fop = &f2fs_file_operations;
		//inode->i_mapping->a_ops = &f2fs_dblock_aops;
		mapping = new Cf2fsDataMapping;
	} 
	else if (S_ISDIR(ptr_inode->i_mode)) 
	{
//		JCASSERT(0);
		typed_inode = static_cast<inode*>(new Cf2fsDirInode(*fi));
		unlock_new_inode(ptr_inode);
		iput(ptr_inode);
		//inode->i_op = &f2fs_dir_inode_operations;
		//inode->i_fop = &f2fs_dir_operations;
		//inode->i_mapping->a_ops = &f2fs_dblock_aops;
		inode_nohighmem(typed_inode);
		mapping = new Cf2fsDataMapping;
	} 
	else if (S_ISLNK(ptr_inode->i_mode)) 
	{
		JCASSERT(0);
		//if (file_is_encrypt(inode))			inode->i_op = &f2fs_encrypted_symlink_inode_operations;
		//else								inode->i_op = &f2fs_symlink_inode_operations;
		inode_nohighmem(ptr_inode);
		//inode->i_mapping->a_ops = &f2fs_dblock_aops;
		mapping = new Cf2fsDataMapping;
	} 
	else if (S_ISCHR(ptr_inode->i_mode) || S_ISBLK(ptr_inode->i_mode) || S_ISFIFO(ptr_inode->i_mode) || S_ISSOCK(ptr_inode->i_mode)) 
	{
		//inode->i_op = &f2fs_special_inode_operations;
		init_special_inode(ptr_inode, ptr_inode->i_mode, ptr_inode->i_rdev);
	} 
	else 
	{
		ret = -EIO;
		goto bad_inode;
	}
	//<YUAN>
	m_inodes.init_inode_mapping(typed_inode, mapping, (m_sb_info->s_type->fs_flags & FS_THP_SUPPORT));
	f2fs_set_inode_flags(typed_inode);
	unlock_new_inode(typed_inode);
	//trace_f2fs_iget(inode);
	return typed_inode;

bad_inode:
	f2fs_inode_synced(typed_inode);
	m_inodes.iget_failed(typed_inode);
	//trace_f2fs_iget_exit(inode, ret);
//	return ERR_PTR(ret);
	return NULL;
}

#else

// 对于f2fs_iget()的整体流程，对于源代码有大幅改动
//		- 源代码中，现在lru中查找是否有ino的inode存在，有则直接返回。
//		没有的话，创建一个空的inode，做基本初始化，并添加到sb_list中。然后调用do_read_inode()读取inode的内容，按内容初始化inode。
//		同时根据inode的类型，分别设置相应的i_op和mapping_op
//		- 由于新的代码采用inode的派生类实现i_op的多态性，必须知道inode类型后才能根据类型创建inode。
//		因此顺序调整，（1）检查是否在lru cache中，（待实现，由于继承类的大小不同，需要考虑如何实现）（2）没有命中的话，则读取磁盘上的inode，（3）根据inode的类型，创建对象，（4）初始化，并且添加到相应的队列中。

f2fs_inode_info* f2fs_sb_info::f2fs_iget(nid_t ino)
{
//	LOG_STACK_TRACE();
	// 查找ino是否已经存在，是则直接返回。
	// (*) 在file system中，同样的ino必须打开相同的对象。否则在移动文件等操作时，计数器会出错。
	f2fs_inode_info * new_node = NULL;
	new_node = m_inodes.iget_locked<f2fs_inode_info>(true, ino);
	if (new_node && !new_node->TestState(I_NEW))
	{	// 找到已经存在的ino，直接返回
		LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, - got inode from cache", new_node, new_node->i_ino);
		return new_node;
	}

	// 用于虚函数实现，针对不同的类型，创建不同的inode对象
	
//	inode* typed_inode = NULL;
	int ret = 0;

	if (ino == F2FS_NODE_INO() || ino == F2FS_META_INO())
	{
		new_node = GetInodeLocked<f2fs_inode_info>((s_type->fs_flags & FS_THP_SUPPORT), ino/*, nullptr*/);
		LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, - create for meta/node", new_node, ino);
		if (!(new_node->TestState(I_NEW))) { return new_node; }
	}
	else
	{
		// （1）查找指定的inode是否存在，如果存在直接返回：TODO
//		if (!(new_node->i_state & I_NEW))
//		{	//<YUAN>无法找到trace_f2fs_iget()
//			return new_node;
//		}
		// （2）如果不存在，创建并且读取
//		LOG_DEBUG(L"read inode by ino=%d", ino);
		ret = do_create_read_inode(new_node, ino);
		LOG_DEBUG(L"[inode_track] add=%p, ino=%d, mode=%X, - read inode from disk", new_node, ino, new_node->i_mode);
		if (ret)
		{
			LOG_ERROR(L"[err] failed on reading inode, ino=%d, err=%d", ino, ret);
			goto bad_inode;
		}
	}
	// <YUAN> 从磁盘读取到的inode添加到hash_table中

//make_now:
	if (0) {}
	else if (ino == F2FS_NODE_INO() || ino == F2FS_META_INO())	{	}
	else if (S_ISREG(new_node->i_mode))	{	}
	else if (S_ISDIR(new_node->i_mode))
	{
		new_node->inode_nohighmem();
	}
	else if (S_ISLNK(new_node->i_mode))
	{
		//if (file_is_encrypt(inode))			inode->i_op = &f2fs_encrypted_symlink_inode_operations;
		//else								inode->i_op = &f2fs_symlink_inode_operations;
		new_node->inode_nohighmem();
	}
	else if (S_ISCHR(new_node->i_mode) || S_ISBLK(new_node->i_mode) || S_ISFIFO(new_node->i_mode) || S_ISSOCK(new_node->i_mode))
	{
		//JCASSERT(0);
		//inode->i_op = &f2fs_special_inode_operations;
		init_special_inode(new_node, new_node->i_mode, new_node->i_rdev);
	}
	else
	{
		ret = -EIO;
		goto bad_inode;
	}

	//<YUAN>
//	m_inodes.init_inode_mapping(new_node, mapping, (this->s_type->fs_flags & FS_THP_SUPPORT));
	f2fs_set_inode_flags(new_node);
	unlock_new_inode(new_node);
	//trace_f2fs_iget(inode);
	return new_node;

bad_inode:
	new_node->f2fs_inode_synced();
	m_inodes.iget_failed(new_node);
	//trace_f2fs_iget_exit(inode, ret);
	return ERR_PTR<f2fs_inode_info>(ret);
}

#endif

f2fs_inode_info* f2fs_sb_info::f2fs_iget_retry(unsigned long ino)
{
	f2fs_inode_info*iinode;
retry:
	iinode = f2fs_iget(ino);
	if (IS_ERR(iinode)) 
	{
		if (PTR_ERR(iinode) == -ENOMEM) 
		{
			//congestion_wait(BLK_RW_ASYNC, DEFAULT_IO_TIMEOUT);
			JCASSERT(0);
			Sleep(20);
			goto retry;
		}
	}
	return iinode;
}

//void f2fs_update_inode(struct inode *inode, struct page *node_page)
void f2fs_inode_info::f2fs_update_inode(page* node_page)
{
	f2fs_inode *ri;
	struct extent_tree *et = extent_tree;

	f2fs_wait_on_page_writeback(node_page, NODE, true, true);
	set_page_dirty(node_page);		// <= 调用此函数是，page已经lock了

	f2fs_inode_synced();

	ri = F2FS_INODE(node_page);

	ri->i_mode = cpu_to_le16(i_mode);
	ri->i_advise = i_advise;
	//ri->i_uid = cpu_to_le32(i_uid_read(this));
	//ri->i_gid = cpu_to_le32(i_gid_read(this));
	ri->i_links = cpu_to_le32(i_nlink);
	ri->i_size = cpu_to_le64(i_size_read(this));
	ri->i_blocks = cpu_to_le64(SECTOR_TO_BLOCK(i_blocks) + 1);
	LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, mode =%X, link=%d - store inode", this, i_ino, ri->i_mode, i_nlink);

	if (et) 
	{
		read_lock(&et->lock);
		set_raw_extent(&et->largest, &ri->i_ext);
		read_unlock(&et->lock);
	}
	else
	{
		memset(&ri->i_ext, 0, sizeof(ri->i_ext));
	}
	set_raw_inline(this, ri);

	ri->i_atime = cpu_to_le64(i_atime);
	ri->i_ctime = cpu_to_le64(i_ctime);
	ri->i_mtime = cpu_to_le64(i_mtime);
	//ri->i_atime_nsec = cpu_to_le32(i_atime.tv_nsec);
	//ri->i_ctime_nsec = cpu_to_le32(i_ctime.tv_nsec);
	//ri->i_mtime_nsec = cpu_to_le32(i_mtime.tv_nsec);
	if (S_ISDIR(i_mode))		ri->i_current_depth = cpu_to_le32(i_current_depth);
	else if (S_ISREG(i_mode))	ri->i_gc_failures =	cpu_to_le16(i_gc_failures[GC_FAILURE_PIN]);

	ri->i_xattr_nid = cpu_to_le32(i_xattr_nid);
	ri->i_flags = cpu_to_le32(i_flags);
	ri->i_pino = cpu_to_le32(i_pino);
	ri->i_generation = cpu_to_le32(i_generation);
	ri->i_dir_level = i_dir_level;

	if (f2fs_has_extra_attr(this))
	{
		ri->_u._s.i_extra_isize = cpu_to_le16(i_extra_isize);

		if (f2fs_sb_has_flexible_inline_xattr(F2FS_I_SB(this)))
			ri->_u._s.i_inline_xattr_size = cpu_to_le16(i_inline_xattr_size);

		if (f2fs_sb_has_project_quota(F2FS_I_SB(this)) &&
			F2FS_FITS_IN_INODE(ri, i_extra_isize, _u._s.i_projid)) 
		{
#if 0 //磁盘配额用，不支持
			projid_t i_projid;
			i_projid = from_kprojid(&init_user_ns, i_projid);
			ri->i_projid = cpu_to_le32(i_projid);
#else
			JCASSERT(0);
#endif //TODO
		}

		if (f2fs_sb_has_inode_crtime(F2FS_I_SB(this)) && F2FS_FITS_IN_INODE(ri, i_extra_isize, _u._s.i_crtime)) 
		{
			ri->_u._s.i_crtime = cpu_to_le64(i_crtime);
			//ri->_u._s.i_crtime_nsec = cpu_to_le32(i_crtime.tv_nsec);
		}

		if (f2fs_sb_has_compression(F2FS_I_SB(this)) &&	F2FS_FITS_IN_INODE(ri, i_extra_isize, _u._s.i_log_cluster_size)) 
		{
			ri->_u._s.i_compr_blocks = cpu_to_le64(atomic_read(&i_compr_blocks));
			ri->_u._s.i_compress_algorithm = i_compress_algorithm;
			ri->_u._s.i_compress_flag = cpu_to_le16(i_compress_flag);
			ri->_u._s.i_log_cluster_size = i_log_cluster_size;
		}
	}

	__set_inode_rdev(ri);

	/* deleted inode */
	if (i_nlink == 0)	clear_inline_node(node_page);

	i_disk_time[0] = i_atime;
	i_disk_time[1] = i_ctime;
	i_disk_time[2] = i_mtime;
	i_disk_time[3] = i_crtime;

#ifdef CONFIG_F2FS_CHECK_FS
	f2fs_inode_chksum_set(F2FS_I_SB(inode), node_page);
#endif
}


//void f2fs_update_inode_page(struct inode *inode)
void f2fs_inode_info::f2fs_update_inode_page(void)
{
	f2fs_sb_info *sbi = F2FS_I_SB(this);
	page *node_page;
retry:
	node_page = sbi->f2fs_get_node_page(i_ino);
	if (IS_ERR(node_page))
	{
		int err = PTR_ERR(node_page);
		if (err == -ENOMEM) 
		{
#if 0 //TODO
			cond_resched();
#endif
			goto retry;
		} 
		else if (err != -ENOENT)
		{
			f2fs_stop_checkpoint(sbi, false);
		}
		return;
	}
	f2fs_update_inode(node_page);
	f2fs_put_page(node_page, 1);
}

//int f2fs_write_inode(struct inode *inode, struct writeback_control *wbc)
int f2fs_inode_info::f2fs_write_inode(writeback_control *wbc)
{
//	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	if (i_ino == m_sbi->F2FS_NODE_INO() || i_ino == m_sbi->F2FS_META_INO())
		return 0;

	/* atime could be updated without dirtying f2fs inode in lazytime mode */
	if (f2fs_is_time_consistent(this) && !is_inode_flag_set(FI_DIRTY_INODE))
		return 0;

	if (!m_sbi->f2fs_is_checkpoint_ready())
		return -ENOSPC;

	/* We need to balance fs here to prevent from producing dirty node pages during the urgent cleaning time when running out of free sections. */
	f2fs_update_inode_page();
	if (wbc && wbc->nr_to_write)
		m_sbi->f2fs_balance_fs( true);
	return 0;
}

//int f2fs_sb_info::write_inode(inode* ii, writeback_control* wbc)
//{
//	//	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
//	f2fs_inode_info* iinode = F2FS_I(ii);
//
//	if (iinode->i_ino == F2FS_NODE_INO() || iinode->i_ino == F2FS_META_INO())
//		return 0;
//
//	/* atime could be updated without dirtying f2fs inode in lazytime mode */
//	if (f2fs_is_time_consistent(iinode) && !iinode->is_inode_flag_set(FI_DIRTY_INODE))
//		return 0;
//
//	if (!f2fs_is_checkpoint_ready())
//		return -ENOSPC;
//
//	/* We need to balance fs here to prevent from producing dirty node pages during the urgent cleaning time when running out of free sections. */
//	iinode->f2fs_update_inode_page();
//	if (wbc && wbc->nr_to_write)	f2fs_balance_fs(true);
//	return 0;
//}


/* Called at the last iput() if i_nlink is zero */
//void f2fs_evict_inode(struct inode *inode)	
void f2fs_sb_info::evict_inode(inode* iinode)
{
	LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, link=%d - evict", iinode, iinode->i_ino, iinode->i_nlink);
//	struct f2fs_sb_info *sbi = F2FS_I_SB(iinode);
	f2fs_inode_info* fi = F2FS_I(iinode);
	nid_t xnid = F2FS_I(iinode)->i_xattr_nid;
	int err = 0;

	/* some remained atomic pages should discarded */
	if (f2fs_is_atomic_file(iinode))
		f2fs_drop_inmem_pages(iinode);

//	trace_f2fs_evict_inode(iinode);
	address_space* mapping = iinode->get_mapping();
	truncate_inode_pages_final(mapping);

	if (iinode->i_ino == F2FS_NODE_INO() || iinode->i_ino == F2FS_META_INO())
		goto out_clear;

	f2fs_bug_on(this, get_dirty_pages(iinode));
	f2fs_remove_dirty_inode(iinode);

	f2fs_destroy_extent_tree(iinode);

	if (iinode->i_nlink || iinode->is_bad_inode())
		goto no_delete;

	err = dquot_initialize(iinode);
	if (err) {
		err = 0;
		set_sbi_flag( SBI_QUOTA_NEED_REPAIR);
	}

	f2fs_remove_ino_entry(this, iinode->i_ino, APPEND_INO);
	f2fs_remove_ino_entry(this, iinode->i_ino, UPDATE_INO);
	f2fs_remove_ino_entry(this, iinode->i_ino, FLUSH_INO);

	sb_start_intwrite(iinode->i_sb);
	fi->set_inode_flag(FI_NO_ALLOC);
	i_size_write(iinode, 0);
retry:
	if (F2FS_HAS_BLOCKS(iinode))
		err = f2fs_truncate(fi);

	if (time_to_inject(this, FAULT_EVICT_INODE))
	{
		f2fs_show_injection_info(this, FAULT_EVICT_INODE);
		err = -EIO;
	}

	if (!err)
	{
		f2fs_lock_op();
		//auto_lock<semaphore_read_lock> lock_op(cp_rwsem);

		err = f2fs_remove_inode_page(iinode);
		f2fs_unlock_op();
		if (err == -ENOENT)
			err = 0;
	}

	/* give more chances, if ENOMEM case */
	if (err == -ENOMEM) 
	{
		err = 0;
		goto retry;
	}

	if (err)
	{
		fi->f2fs_update_inode_page();
		// 不支持quot
		//if (dquot_initialize_needed(iinode))	set_sbi_flag(this, SBI_QUOTA_NEED_REPAIR);
	}
	sb_end_intwrite(iinode->i_sb);
no_delete:
	//dquot_drop(iinode);

	stat_dec_inline_xattr(iinode);
	stat_dec_inline_dir(iinode);
	stat_dec_inline_inode(iinode);
	stat_dec_compr_inode(iinode);
	stat_sub_compr_blocks(iinode, atomic_read(&F2FS_I(iinode)->i_compr_blocks));

	if (likely(!f2fs_cp_error() &&	!is_sbi_flag_set( SBI_CP_DISABLED)))
		f2fs_bug_on(this, is_inode_flag_set(iinode, FI_DIRTY_INODE));
	else
		fi->f2fs_inode_synced();

	/* for the case f2fs_new_inode() was failed, .i_ino is zero, skip it */
	if (iinode->i_ino)
		invalidate_mapping_pages(NODE_MAPPING(this), iinode->i_ino,	iinode->i_ino);
	if (xnid)
		invalidate_mapping_pages(NODE_MAPPING(this), xnid, xnid);
	if (iinode->i_nlink)
	{
		if (is_inode_flag_set(iinode, FI_APPEND_WRITE))
			f2fs_add_ino_entry(this, iinode->i_ino, APPEND_INO);
		if (is_inode_flag_set(iinode, FI_UPDATE_WRITE))
			f2fs_add_ino_entry(this, iinode->i_ino, UPDATE_INO);
	}
	if (is_inode_flag_set(iinode, FI_FREE_NID))
	{
		f2fs_alloc_nid_failed(this, iinode->i_ino);
		clear_inode_flag(fi, FI_FREE_NID);
	} else 
	{
		/* If xattr nid is corrupted, we can reach out error condition, err & !f2fs_exist_written_data(this, inode->i_ino, ORPHAN_INO)). In that case, f2fs_check_nid_range() is enough to give a clue.	 */
	}
out_clear:
	fscrypt_put_encryption_info(iinode);
//	fsverity_cleanup_inode(iinode);
	clear_inode(iinode);
}

/* caller should call f2fs_lock_op() */
void f2fs_handle_failed_inode(inode *node)
{
	f2fs_sb_info *sbi = F2FS_I_SB(node);
	node_info ni;
	int err;

	f2fs_inode_info* iinode = F2FS_I(node);

	/* clear nlink of inode in order to release resource of inode immediately.	 */
	iinode->clear_nlink();

	/* we must call this to avoid inode being remained as dirty, resulting in a panic when flushing dirty inodes in
	   gdirty_list. */
	iinode->f2fs_update_inode_page();
	iinode->f2fs_inode_synced();

	/* don't make bad inode, since it becomes a regular file. */
	unlock_new_inode(iinode);

	/* Note: we should add inode to orphan list before f2fs_unlock_op() so we can prevent losing this orphan when
	   encoutering checkpoint and following suddenly power-off.	 */
	err = NM_I(sbi)->f2fs_get_node_info( iinode->i_ino, &ni);
	if (err) 
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		LOG_WARNING(L"May loss orphan inode, run fsck to fix.");
		goto out;
	}

	if (ni.blk_addr != NULL_ADDR) 
	{
		err = sbi->f2fs_acquire_orphan_inode();
		if (err) 
		{
			sbi->set_sbi_flag(SBI_NEED_FSCK);
			LOG_WARNING(L"Too many orphan inodes, run fsck to fix.");
		} 
		else {	sbi->f2fs_add_orphan_inode(iinode);	}
		sbi->nm_info->f2fs_alloc_nid_done(node->i_ino);
	} 
	else {	iinode->set_inode_flag( FI_FREE_NID);	}

out:
	sbi->f2fs_unlock_op();
	/* iput will drop the inode object */
	iput(iinode);
}



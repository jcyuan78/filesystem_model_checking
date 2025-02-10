///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"
// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/inline.c
 * Copyright (c) 2013, Intel Corporation
 * Authors: Huajun Li <huajun.li@intel.com>
 *          Haicheng Li <haicheng.li@intel.com>
 */

#include "../../include/f2fs.h"
#include "../../include/f2fs_fs.h"
#include "../../include/f2fs-filesystem.h"

//#include <linux/fs.h>
//#include <linux/f2fs_fs.h>
//#include <linux/fiemap.h>
//
//#include "f2fs.h"
#include "node.h"
//#include <trace/events/f2fs.h>

LOCAL_LOGGER_ENABLE(L"f2fs.inline", LOGGER_LEVEL_DEBUGINFO);


bool f2fs_may_inline_data(f2fs_inode_info *inode)
{
	if (f2fs_is_atomic_file(inode)) 	return false;
	if (!S_ISREG(inode->i_mode) && !S_ISLNK(inode->i_mode))		return false;
	if (i_size_read(inode) > MAX_INLINE_DATA(F2FS_I(inode)))		return false;
	if (f2fs_post_read_required(inode))		return false;
	return true;
}

bool f2fs_may_inline_dentry(struct inode *inode)
{
	if (!test_opt(F2FS_I_SB(inode), INLINE_DENTRY))		return false;
	if (!S_ISDIR(inode->i_mode))		return false;
	return true;
}

void f2fs_do_read_inline_data(struct page *page, struct page *ipage)
{
	struct inode *inode = page->mapping->host;
	void *src_addr, *dst_addr;

	if (PageUptodate(page))
		return;

	f2fs_bug_on(F2FS_P_SB(page), page->index);

	zero_user_segment(page, MAX_INLINE_DATA(F2FS_I(inode)), PAGE_SIZE);

	/* Copy the whole inline data block */
	src_addr = F2FS_I(inode)->inline_data_addr(ipage);
//	dst_addr = kmap_atomic(page);
	dst_addr = page_address<void>(page);
	memcpy(dst_addr, src_addr, MAX_INLINE_DATA(F2FS_I(inode)));
	flush_dcache_page(page);
//	kunmap_atomic(dst_addr);
	if (!PageUptodate(page))
		SetPageUptodate(page);
}

void f2fs_truncate_inline_inode(f2fs_inode_info *iinode, page *ipage, u64 from)
{
	BYTE *addr;

	if (from >= MAX_INLINE_DATA(iinode))
		return;

	addr = (BYTE*)iinode->inline_data_addr(ipage);

	f2fs_wait_on_page_writeback(ipage, NODE, true, true);
	memset(addr + from, 0, MAX_INLINE_DATA(iinode) - from);
	set_page_dirty(ipage);

	if (from == 0)	clear_inode_flag(iinode, FI_DATA_EXIST);
}

int f2fs_read_inline_data(f2fs_inode_info *inode, struct page *ppage)
{
	page *ipage;
	f2fs_sb_info* sbi = F2FS_I_SB(inode);

	ipage = sbi->f2fs_get_node_page(inode->i_ino);
	if (IS_ERR(ipage))
	{
		unlock_page(ppage);
		return PTR_ERR(ipage);
	}

	if (!f2fs_has_inline_data(inode))
	{
		f2fs_put_page(ipage, 1);
		return -EAGAIN;
	}

	if (ppage->index)		zero_user_segment(ppage, 0, PAGE_SIZE);
	else		f2fs_do_read_inline_data(ppage, ipage);

	if (!PageUptodate(ppage))		SetPageUptodate(ppage);
	f2fs_put_page(ipage, 1);
	unlock_page(ppage);
	return 0;
}

int f2fs_convert_inline_page(struct dnode_of_data *dn, struct page *page)
{
	struct f2fs_io_info fio;
		fio.sbi = F2FS_I_SB(dn->inode);
		fio.ino = dn->inode->i_ino;
		fio.type = DATA;
		fio.op = REQ_OP_WRITE;
		fio.op_flags = REQ_SYNC | REQ_PRIO;
		fio.page = page;
		fio.encrypted_page = NULL;
		fio.io_type = FS_DATA_IO;
	struct node_info ni;
	int dirty, err;

	if (!f2fs_exist_data(dn->inode))		goto clear_out;

	err = f2fs_reserve_block(dn, 0);
	if (err)		return err;

	err = NM_I(fio.sbi)->f2fs_get_node_info(dn->nid, &ni);
	if (err) 
	{
		f2fs_truncate_data_blocks_range(dn, 1);
		f2fs_put_dnode(dn);
		return err;
	}

	fio.version = ni.version;

	if (unlikely(dn->data_blkaddr != NEW_ADDR))
	{
		f2fs_put_dnode(dn);
		set_sbi_flag(fio.sbi, SBI_NEED_FSCK);
		LOG_WARNING(L"corrupted inline inode ino=%lx, i_addr[0]:0x%x, run fsck to fix.", dn->inode->i_ino, dn->data_blkaddr);
		return -EFSCORRUPTED;
	}

	f2fs_bug_on(F2FS_P_SB(page), PageWriteback(page));

	f2fs_do_read_inline_data(page, dn->inode_page);
	set_page_dirty(page);

	/* clear dirty state */
	dirty = clear_page_dirty_for_io(page);

	/* write data page to try to make data consistent */
	set_page_writeback(page);
	ClearPageError(page);
	fio.old_blkaddr = dn->data_blkaddr;
	set_inode_flag(dn->inode, FI_HOT_DATA);
	f2fs_outplace_write_data(dn, &fio);
	f2fs_wait_on_page_writeback(page, DATA, true, true);
	if (dirty) 
	{
		LOG_TRACK(L"page.dirty", L" dec: inode=%d, page=%d", dn->inode->i_ino, page->index);
		inode_dec_dirty_pages(dn->inode);
		f2fs_remove_dirty_inode(dn->inode);
	}

	/* this converted inline_data should be recovered. */
	set_inode_flag(dn->inode, FI_APPEND_WRITE);

	/* clear inline data and flag after data writeback */
	f2fs_truncate_inline_inode(dn->inode, dn->inode_page, 0);
	clear_inline_node(dn->inode_page);
clear_out:
	stat_dec_inline_inode(dn->inode);
	clear_inode_flag(dn->inode, FI_INLINE_DATA);
	f2fs_put_dnode(dn);
	return 0;
}

//int f2fs_convert_inline_inode(struct inode *inode)
int f2fs_inode_info::f2fs_convert_inline_inode(void)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(this);
	struct dnode_of_data dn;
	struct page *ipage, *page;
	int err = 0;

	if (!f2fs_has_inline_data(this) || f2fs_hw_is_readonly(sbi) || sbi->f2fs_readonly())
		return 0;

	err = dquot_initialize(this);
	if (err)
		return err;

	page = f2fs_grab_cache_page(i_mapping, 0, false);
	if (!page)
		return -ENOMEM;

	sbi->f2fs_lock_op();
	//auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);

	ipage = sbi->f2fs_get_node_page( i_ino);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto out;
	}

	dn.set_new_dnode(this, ipage, ipage, 0);

	if (f2fs_has_inline_data(this))		err = f2fs_convert_inline_page(&dn, page);

	f2fs_put_dnode(&dn);
out:
	sbi->f2fs_unlock_op();

	f2fs_put_page(page, 1);

	if (!err)
		sbi->f2fs_balance_fs(dn.node_changed);

	return err;
}

int f2fs_write_inline_data(f2fs_inode_info *inode, struct page *page)
{
	void *src_addr, *dst_addr;
	struct dnode_of_data dn;
	int err;

	dn.set_new_dnode(inode, NULL, NULL, 0);
	err = f2fs_get_dnode_of_data(&dn, 0, LOOKUP_NODE);
	if (err)
		return err;

	if (!f2fs_has_inline_data(inode)) 
	{
		f2fs_put_dnode(&dn);
		return -EAGAIN;
	}

	f2fs_bug_on(F2FS_I_SB(inode), page->index);

	f2fs_wait_on_page_writeback(dn.inode_page, NODE, true, true);
//	src_addr = kmap_atomic(page);
	src_addr = page_address<void>(page);
	dst_addr = F2FS_I(inode)->inline_data_addr(dn.inode_page);
	memcpy(dst_addr, src_addr, MAX_INLINE_DATA(F2FS_I(inode)));
//	kunmap_atomic(src_addr);
	set_page_dirty(dn.inode_page);

	f2fs_clear_page_cache_dirty_tag(page);
	f2fs_inode_info* fi = F2FS_I(inode);
	fi->set_inode_flag(FI_APPEND_WRITE);
	fi->set_inode_flag(FI_DATA_EXIST);

	clear_inline_node(dn.inode_page);
	f2fs_put_dnode(&dn);
	return 0;
}


int f2fs_recover_inline_data(f2fs_inode_info *iinode, page *npage)
{
	f2fs_sb_info *sbi = F2FS_I_SB(iinode);
	f2fs_inode *ri = NULL;
	void *src_addr, *dst_addr;
	page *ipage;

	/*
	 * The inline_data recovery policy is as follows.
	 * [prev.] [next] of inline_data flag
	 *    o       o  -> recover inline_data
	 *    o       x  -> remove inline_data, and then recover data blocks
	 *    x       o  -> remove data blocks, and then recover inline_data
	 *    x       x  -> recover data blocks
	 */
	if (IS_INODE(npage))	ri = F2FS_INODE(npage);

//	f2fs_inode_info* fi = F2FS_I(iinode);
	if (f2fs_has_inline_data(iinode) &&	ri && (ri->i_inline & F2FS_INLINE_DATA))
	{
process_inline:
		ipage = sbi->f2fs_get_node_page(iinode->i_ino);
		if (IS_ERR(ipage))
			return PTR_ERR(ipage);

		f2fs_wait_on_page_writeback(ipage, NODE, true, true);

		src_addr = iinode->inline_data_addr(npage);
		dst_addr = iinode->inline_data_addr(ipage);
		memcpy(dst_addr, src_addr, MAX_INLINE_DATA(iinode));

		iinode->set_inode_flag(FI_INLINE_DATA);
		iinode->set_inode_flag(FI_DATA_EXIST);

		set_page_dirty(ipage);
		f2fs_put_page(ipage, 1);
		return 1;
	}

	if (f2fs_has_inline_data(iinode)) 
	{
		ipage = sbi->f2fs_get_node_page(iinode->i_ino);
		if (IS_ERR(ipage))		return PTR_ERR(ipage);
		f2fs_truncate_inline_inode(iinode, ipage, 0);
		stat_dec_inline_inode(iinode);
		clear_inode_flag(iinode, FI_INLINE_DATA);
		f2fs_put_page(ipage, 1);
	} 
	else if (ri && (ri->i_inline & F2FS_INLINE_DATA)) 
	{
		int ret = iinode->f2fs_truncate_blocks(0, false);
		if (ret)			return ret;
		stat_inc_inline_inode(iinode);
		goto process_inline;
	}
	return 0;
}

//f2fs_dir_entry* f2fs_find_in_inline_dir(inode* dir, const f2fs_filename* fname, page** res_page)
f2fs_dir_entry* Cf2fsDirInode::f2fs_find_in_inline_dir(const f2fs_filename* fname, page** res_page)
{
//#define ERR_CAST(x) reinterpret_cast<page*>(x)
//	f2fs_sb_info *sbi = F2FS_SB(this->i_sb);
	f2fs_dir_entry *de;
	f2fs_dentry_ptr d;
	page *ipage;
	void *inline_dentry;

	ipage = m_sbi->f2fs_get_node_page( this->i_ino);
	if (IS_ERR(ipage)) 
	{
		*res_page = ipage;
		return NULL;
	}

	inline_dentry = inline_data_addr(ipage);

	make_dentry_ptr_inline(&d, inline_dentry);
	de = f2fs_find_target_dentry(&d, fname, NULL);
	unlock_page(ipage);
	if (IS_ERR(de)) 
	{
		*res_page = ERR_PTR<page>(PTR_ERR(de));
		de = NULL;
	}
	if (de)		*res_page = ipage;
	else		f2fs_put_page(ipage, 0);
	return de;
//#undef ERR_CAST
}

//int f2fs_make_empty_inline_dir(struct inode *inode, struct inode *parent, struct page *ipage)
int Cf2fsDirInode::f2fs_make_empty_inline_dir(inode* parent, page* ipage)
{
	f2fs_dentry_ptr d;
	void *inline_dentry;

	inline_dentry = inline_data_addr(ipage);

	make_dentry_ptr_inline(&d, inline_dentry);
	f2fs_do_make_empty_dir(parent, &d);

	set_page_dirty(ipage);

	/* update i_size to MAX_INLINE_DATA */
	if (i_size_read(this) < MAX_INLINE_DATA(this))
		f2fs_i_size_write(MAX_INLINE_DATA(this));
	return 0;
}

/* NOTE: ipage is grabbed by caller, but if any error occurs, we should release ipage in this function. */
//static int f2fs_move_inline_dirents(f2fs_inode_info *dir, struct page *ipage, void *inline_dentry)
int Cf2fsDirInode::f2fs_move_inline_dirents(page* ipage, void* inline_dentry)
{
	struct page *page;
	struct dnode_of_data dn;
	struct f2fs_dentry_block *dentry_blk;
	struct f2fs_dentry_ptr src, dst;
	int err;

	page = f2fs_grab_cache_page(i_mapping, 0, true);
	if (!page) {
		f2fs_put_page(ipage, 1);
		return -ENOMEM;
	}

	dn.set_new_dnode(this, ipage, NULL, 0);
	err = f2fs_reserve_block(&dn, 0);
	if (err)
		goto out;

	if (unlikely(dn.data_blkaddr != NEW_ADDR)) {
		f2fs_put_dnode(&dn);
		set_sbi_flag(F2FS_P_SB(page), SBI_NEED_FSCK);
		//f2fs_warn(F2FS_P_SB(page), "%s: corrupted inline inode ino=%lx, i_addr[0]:0x%x, run fsck to fix.",
		//	  __func__, dir->i_ino, dn.data_blkaddr);
		LOG_WARNING(L"corrupted inline inode ino=%lx, i_addr[0]:0x%x, run fsck to fix.", i_ino, dn.data_blkaddr);
		err = -EFSCORRUPTED;
		goto out;
	}

	f2fs_wait_on_page_writeback(page, DATA, true, true);

	dentry_blk = page_address<f2fs_dentry_block>(page);

	make_dentry_ptr_inline(&src, inline_dentry);
	make_dentry_ptr_block(&dst, dentry_blk);

	/* copy data from inline dentry block to new dentry block */
	memcpy(dst.bitmap, src.bitmap, src.nr_bitmap);
	memset((BYTE*)(dst.bitmap) + src.nr_bitmap, 0, dst.nr_bitmap - src.nr_bitmap);
	/*
	 * we do not need to zero out remainder part of dentry and filename
	 * field, since we have used bitmap for marking the usage status of
	 * them, besides, we can also ignore copying/zeroing reserved space
	 * of dentry block, because them haven't been used so far.
	 */
	memcpy(dst.dentry, src.dentry, SIZE_OF_DIR_ENTRY * src.max);
	memcpy(dst.filename, src.filename, src.max * F2FS_SLOT_LEN);

	if (!PageUptodate(page))
		SetPageUptodate(page);
	set_page_dirty(page);

	/* clear inline dir and flag after data writeback */
	f2fs_truncate_inline_inode(this, ipage, 0);

	stat_dec_inline_dir(this);
	clear_inode_flag(this, FI_INLINE_DENTRY);

	/* should retrieve reserved space which was used to keep inline_dentry's structure for backward compatibility. */
	if (!f2fs_sb_has_flexible_inline_xattr(F2FS_I_SB(this)) && !f2fs_has_inline_xattr(this))
		i_inline_xattr_size = 0;

	f2fs_i_depth_write(1);
	if (i_size_read(this) < PAGE_SIZE)
		f2fs_i_size_write(PAGE_SIZE);
out:
	f2fs_put_page(page, 1);
	return err;
}

static int f2fs_add_inline_entries(struct inode *dir, void *inline_dentry)
{
#if 0 //TODO
	struct f2fs_dentry_ptr d;
	unsigned long bit_pos = 0;
	int err = 0;

	make_dentry_ptr_inline(dir, &d, inline_dentry);

	while (bit_pos < d.max) {
		struct f2fs_dir_entry *de;
		struct f2fs_filename fname;
		nid_t ino;
		umode_t fake_mode;

		if (!test_bit_le(bit_pos, (UINT8*)d.bitmap))
		{
			bit_pos++;
			continue;
		}

		de = &d.dentry[bit_pos];

		if (unlikely(!de->name_len)) {
			bit_pos++;
			continue;
		}

		/*
		 * We only need the disk_name and hash to move the dentry.
		 * We don't need the original or casefolded filenames.
		 */
		memset(&fname, 0, sizeof(fname));
		fname.disk_name.name = d.filename[bit_pos];
		fname.disk_name.len = le16_to_cpu(de->name_len);
		fname.hash = de->hash_code;

		ino = le32_to_cpu(de->ino);
		fake_mode = f2fs_get_de_type(de) << S_SHIFT;

		err = dir->f2fs_add_regular_entry(&fname, NULL, ino, fake_mode);
		if (err)
			goto punch_dentry_pages;

		bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de->name_len));
	}
	return 0;
punch_dentry_pages:
	truncate_inode_pages(&dir->i_data, 0);
	f2fs_truncate_blocks(dir, 0, false);
	f2fs_remove_dirty_inode(dir);
	return err;
#else
	JCASSERT(0);
	return 0;
#endif
}


static int f2fs_move_rehashed_dirents(f2fs_inode_info *dir, struct page *ipage, void *inline_dentry)
{
	BYTE *backup_dentry;
	int err;

	backup_dentry = f2fs_kmalloc<BYTE>(F2FS_I_SB(dir), MAX_INLINE_DATA(dir)/*, GFP_F2FS_ZERO*/);
	if (!backup_dentry)
	{
		f2fs_put_page(ipage, 1);
		return -ENOMEM;
	}

	memcpy(backup_dentry, inline_dentry, MAX_INLINE_DATA(dir));
	f2fs_truncate_inline_inode(dir, ipage, 0);

	unlock_page(ipage);

	err = f2fs_add_inline_entries(dir, backup_dentry);
	if (err)
		goto recover;

	lock_page(ipage);

	stat_dec_inline_dir(dir);
	clear_inode_flag(dir, FI_INLINE_DENTRY);

	/* should retrieve reserved space which was used to keep inline_dentry's structure for backward compatibility. */
	if (!f2fs_sb_has_flexible_inline_xattr(F2FS_I_SB(dir)) && !f2fs_has_inline_xattr(dir))
		F2FS_I(dir)->i_inline_xattr_size = 0;

	f2fs_kvfree(backup_dentry);
	return 0;
recover:
	lock_page(ipage);
	f2fs_wait_on_page_writeback(ipage, NODE, true, true);
	memcpy(inline_dentry, backup_dentry, MAX_INLINE_DATA(dir));
	dir->f2fs_i_depth_write(0);
	dir->f2fs_i_size_write(MAX_INLINE_DATA(dir));
	set_page_dirty(ipage);
	f2fs_put_page(ipage, 1);

	f2fs_kvfree(backup_dentry);
	return err;
}


static int do_convert_inline_dir(f2fs_inode_info *dir, struct page *ipage, void *inline_dentry)
{
	Cf2fsDirInode* dd = dynamic_cast<Cf2fsDirInode*>(dir);
	JCASSERT(dd);
	if (!dir->i_dir_level) 	return dd->f2fs_move_inline_dirents(ipage, inline_dentry);
	else					return f2fs_move_rehashed_dirents(dir, ipage, inline_dentry);
}

//int f2fs_try_convert_inline_dir(struct inode *dir, struct dentry *dentry)
int Cf2fsDirInode::f2fs_try_convert_inline_dir(dentry* ddentry)
{
	//struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	page *ipage;
	f2fs_filename fname;
	void *inline_dentry = NULL;
	int err = 0;

	if (!f2fs_has_inline_dentry())		return 0;

	//m_sbi->f2fs_lock_op();
	auto_lock<f2fs_sb_info::auto_lock_op> lock_op(*m_sbi);

	err = f2fs_setup_filename(&ddentry->d_name, 0, &fname);
	if (err)	return err; // goto out;

	ipage = m_sbi->f2fs_get_node_page( this->i_ino);
	if (IS_ERR(ipage))
	{
		err = PTR_ERR(ipage);
//		goto out_fname;
		return err;
	}

	if (f2fs_has_enough_room(ipage, &fname)) 
	{
		f2fs_put_page(ipage, 1);
//		goto out_fname;
		return err;
	}

	inline_dentry = inline_data_addr(ipage);

	err = do_convert_inline_dir(this, ipage, inline_dentry);
	if (!err)	f2fs_put_page(ipage, 1);
//out_fname:
//	f2fs_free_filename(&fname);
//out:
//	m_sbi->f2fs_unlock_op();
	return err;
}

//int f2fs_add_inline_entry(inode *dir, const f2fs_filename *fname, f2fs_inode_info *inode, nid_t ino, umode_t mode)
// ��inode��ӵ�this�У��ļ���Ϊfname
//	this��Ϊ���ڵ��inode, �൱��ԭ�����е�dir
//	iinode��Ϊ��Ҫ���ӵ��ӽڵ��inode���൱��ԭ�����е�inode
int Cf2fsDirInode::f2fs_add_inline_entry(const f2fs_filename *fname, f2fs_inode_info *iinode, nid_t ino, umode_t mode)
{
	f2fs_sb_info* sbi = m_sbi;
	page *ipage;
	unsigned int bit_pos;
	void *inline_dentry = NULL;
	f2fs_dentry_ptr d;
	int slots = GET_DENTRY_SLOTS(fname->disk_name.len);
	page *ppage = NULL;
	int err = 0;
	
	// ipageΪ���ڵ�inode��Ӧ��ondisk data 
	ipage = sbi->f2fs_get_node_page(i_ino);
	if (IS_ERR(ipage))		return PTR_ERR(ipage);

	inline_dentry = inline_data_addr(ipage);
	make_dentry_ptr_inline(&d, inline_dentry);

	bit_pos = f2fs_room_for_filename(d.bitmap, slots, d.max);
	if (bit_pos >= d.max) 
	{
		err = do_convert_inline_dir(this, ipage, inline_dentry);
		if (err)	return err;
		err = -EAGAIN;
		goto out;
	}

	if (iinode)
	{
		down_write(&iinode->i_sem);
		// ppageΪiinode���ڵ�page,
		ppage = iinode->f2fs_init_inode_metadata(this, fname, ipage);
		if (IS_ERR(ppage)) 
		{
			err = PTR_ERR(ppage);
			goto fail;
		}
	}

	f2fs_wait_on_page_writeback(ipage, NODE, true, true);
	// �������Ϣ���µ� d��
	f2fs_update_dentry(ino, mode, &d, &fname->disk_name, fname->hash, bit_pos);
	set_page_dirty(ipage);

	/* we don't need to mark_inode_dirty now */
	if (iinode) 
	{
		iinode->f2fs_i_pino_write(i_ino);
		/* synchronize inode ppage's data from inode cache */
		if (iinode->is_inode_flag_set(FI_NEW_INODE))
			iinode->f2fs_update_inode( ppage);
		f2fs_put_page(ppage, 1);
	}

	f2fs_update_parent_metadata(iinode, 0);
fail:
	if (iinode)		up_write(&iinode->i_sem);
out:
	f2fs_put_page(ipage, 1);
	return err;
}


//void f2fs_delete_inline_entry(f2fs_dir_entry *dentry, page *ppage, f2fs_inode_info*dir, f2fs_inode_info*iinode)
void Cf2fsDirInode::f2fs_delete_inline_entry(f2fs_dir_entry* dentry, page* ppage, f2fs_inode_info* iinode)

{
	struct f2fs_dentry_ptr d;
	void *inline_dentry;
	int slots = GET_DENTRY_SLOTS(le16_to_cpu(dentry->name_len));
	unsigned int bit_pos;
	int i;

	lock_page(ppage);
	f2fs_wait_on_page_writeback(ppage, NODE, true, true);

	inline_dentry = this->inline_data_addr(ppage);
//	Cf2fsDirInode* dd = dynamic_cast<Cf2fsDirInode*>(this);
	this->make_dentry_ptr_inline(&d, inline_dentry);

	bit_pos = dentry - d.dentry;
	for (i = 0; i < slots; i++)
		__clear_bit_le(bit_pos + i, d.bitmap);

	set_page_dirty(ppage);
	f2fs_put_page(ppage, 1);

	this->i_ctime = this->i_mtime = current_time(this);
	this->f2fs_mark_inode_dirty_sync(false);

	if (iinode) 	f2fs_drop_nlink(this, iinode);
}




//bool f2fs_empty_inline_dir(struct inode *dir)
bool Cf2fsDirInode::f2fs_empty_inline_dir(void) const
{
//	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	page *ipage;
	unsigned int bit_pos = 2;
	void *inline_dentry;
	f2fs_dentry_ptr d;

	ipage = m_sbi->f2fs_get_node_page( this->i_ino);
	if (IS_ERR(ipage))		return false;

	inline_dentry = inline_data_addr(ipage);
	const_cast<Cf2fsDirInode*>(this)->make_dentry_ptr_inline(&d, inline_dentry);

	bit_pos = find_next_bit_le((UINT8*)d.bitmap, d.max, bit_pos);

	f2fs_put_page(ipage, 1);
	if (bit_pos < d.max)	return false;
	return true;
}
#if 0 //TODO

int f2fs_read_inline_dir(struct file *file, struct dir_context *ctx,
				struct fscrypt_str *fstr)
{
	struct inode *inode = file_inode(file);
	struct page *ipage = NULL;
	struct f2fs_dentry_ptr d;
	void *inline_dentry = NULL;
	int err;

	make_dentry_ptr_inline(inode, &d, inline_dentry);

	if (ctx->pos == d.max)
		return 0;

	ipage = f2fs_get_node_page(F2FS_I_SB(inode), inode->i_ino);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	/*
	 * f2fs_readdir was protected by inode.i_rwsem, it is safe to access
	 * ipage without page's lock held.
	 */
	unlock_page(ipage);

	inline_dentry = inline_data_addr(inode, ipage);

	make_dentry_ptr_inline(inode, &d, inline_dentry);

	err = f2fs_fill_dentries(ctx, &d, 0, fstr);
	if (!err)
		ctx->pos = d.max;

	f2fs_put_page(ipage, 0);
	return err < 0 ? err : 0;
}

int f2fs_inline_data_fiemap(struct inode *inode,
		struct fiemap_extent_info *fieinfo, __u64 start, __u64 len)
{
	__u64 byteaddr, ilen;
	__u32 flags = FIEMAP_EXTENT_DATA_INLINE | FIEMAP_EXTENT_NOT_ALIGNED |
		FIEMAP_EXTENT_LAST;
	struct node_info ni;
	struct page *ipage;
	int err = 0;

	ipage = f2fs_get_node_page(F2FS_I_SB(inode), inode->i_ino);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	if ((S_ISREG(inode->i_mode) || S_ISLNK(inode->i_mode)) &&
				!f2fs_has_inline_data(inode)) {
		err = -EAGAIN;
		goto out;
	}

	if (S_ISDIR(inode->i_mode) && !inode->f2fs_has_inline_dentry()) {
		err = -EAGAIN;
		goto out;
	}

	ilen = min_t(size_t, MAX_INLINE_DATA(inode), i_size_read(inode));
	if (start >= ilen)
		goto out;
	if (start + len < ilen)
		ilen = start + len;
	ilen -= start;

	err = f2fs_get_node_info(F2FS_I_SB(inode), inode->i_ino, &ni);
	if (err)
		goto out;

	byteaddr = (__u64)ni.blk_addr << inode->i_sb->s_blocksize_bits;
	byteaddr += (char *)inline_data_addr(inode, ipage) -
					(char *)F2FS_INODE(ipage);
	err = fiemap_fill_next_extent(fieinfo, start, byteaddr, ilen, flags);
	trace_f2fs_fiemap(inode, start, byteaddr, ilen, flags, err);
out:
	f2fs_put_page(ipage, 1);
	return err;
}
#endif
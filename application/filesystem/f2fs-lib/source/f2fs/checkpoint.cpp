///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"
// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/checkpoint.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */

#include <linux-fs-wrapper.h>
//#include <linux/fs.h>
//#include <linux/bio.h>
//#include <linux/mpage.h>
//#include <linux/writeback.h>
//#include <linux/blkdev.h>
//#include <linux/f2fs_fs.h>
#include "../../include/f2fs_fs.h"
//#include <linux/pagevec.h>
//#include <linux/swap.h>
//#include <linux/kthread.h>
//
//#include "f2fs.h"
#include "../../include/f2fs.h"
#include "../../include/f2fs-filesystem.h"
#include "node.h"
#include "segment.h"
//#include <trace/events/f2fs.h>
#include "../mapping.h"
#include <boost/cast.hpp>

LOCAL_LOGGER_ENABLE(L"f2fs.checkpoint", LOGGER_LEVEL_DEBUGINFO);



static struct kmem_cache *ino_entry_slab;
struct kmem_cache *f2fs_inode_entry_slab;

void f2fs_stop_checkpoint(struct f2fs_sb_info *sbi, bool end_io)
{
	f2fs_build_fault_attr(sbi, 0, 0);
	set_ckpt_flags(sbi, CP_ERROR_FLAG);
	if (!end_io)		f2fs_flush_merged_writes(sbi);
}

/* We guarantee no failure on the returned page. */
page *f2fs_grab_meta_page(f2fs_sb_info *sbi, pgoff_t index)
{
	address_space *mapping = META_MAPPING(sbi);
	page *ppage;
repeat:
	ppage = f2fs_grab_cache_page(mapping, index, false);
	if (!ppage) 
	{
//		cond_resched();
		goto repeat;
	}
	f2fs_wait_on_page_writeback(ppage, META, true, true);
	if (!PageUptodate(ppage))
		SetPageUptodate(ppage);
	return ppage;
}

page *f2fs_sb_info::__get_meta_page(pgoff_t index, bool is_meta)
{
	address_space *mapping = META_MAPPING(this);
	page *ppage;

	f2fs_io_info fio;
	fio.sbi = this;
	fio.type = META;
	fio.op = REQ_OP_READ;
	fio.op_flags = REQ_META | REQ_PRIO;
	fio.old_blkaddr = index;
	fio.new_blkaddr = index;
	fio.encrypted_page = NULL;
	fio.is_por = !is_meta;
	int err;

	if (unlikely(!is_meta))		fio.op_flags &= ~REQ_META;
repeat:
	ppage = f2fs_grab_cache_page(mapping, index, false);
	if (!ppage) 
	{
//		cond_resched();
		goto repeat;
	}
#ifdef _DEBUG
	jcvos::Utf8ToUnicode(ppage->m_type, "meta");
	LOG_DEBUG(L"get page: page=%llX, type=%s, index=0x%X", ppage, ppage->m_type.c_str(), ppage->index);
#endif
	if (PageUptodate(ppage))		goto out;

	fio.page = ppage;

	err = f2fs_submit_page_bio(&fio);
	if (err) 
	{
		f2fs_put_page(ppage, 1);
//		return ERR_PTR(err);
//		THROW_ERROR(ERR_APP, L"failed on submit ppage io");
		LOG_ERROR(L"[err] failed on submit page io");
		return NULL;
	}

	f2fs_update_iostat(this, FS_META_READ_IO, F2FS_BLKSIZE);

	lock_page(ppage);
	if (unlikely(ppage->mapping != mapping)) 
	{
		f2fs_put_page(ppage, 1);
		goto repeat;
	}

	if (unlikely(!PageUptodate(ppage)))
	{
		f2fs_put_page(ppage, 1);
//		return ERR_PTR(-EIO);
//		THROW_ERROR(ERR_APP, L"failed on update ppage");
		LOG_ERROR(L"[err] failed on update page");
		return NULL;
	}
out:
	return ppage;
}

page *f2fs_sb_info::f2fs_get_meta_page(pgoff_t index)
{
	return __get_meta_page( index, true);
}

page *f2fs_get_meta_page_retry(struct f2fs_sb_info *sbi, pgoff_t index)
{
	struct page *page;
	int count = 0;

retry:
	page = sbi->__get_meta_page(index, true);
	if (IS_ERR(page)) 
	{
		if (PTR_ERR(page) == -EIO && ++count <= DEFAULT_RETRY_IO_COUNT)			goto retry;
		f2fs_stop_checkpoint(sbi, false);
	}
	return page;
}

/* for POR only */
struct page *f2fs_get_tmp_page(struct f2fs_sb_info *sbi, pgoff_t index)
{
	return sbi->__get_meta_page( index, false);
}

static bool __is_bitmap_valid(f2fs_sb_info *sbi, block_t blkaddr, int type)
{
	struct seg_entry *se;
	unsigned int segno, offset;
	bool exist;

	if (type != DATA_GENERIC_ENHANCE && type != DATA_GENERIC_ENHANCE_READ)
		return true;

	segno = GET_SEGNO(sbi, blkaddr);
	offset = GET_BLKOFF_FROM_SEG0(sbi, blkaddr);
	se = sbi->get_seg_entry( segno);

	exist = f2fs_test_bit(offset, se->cur_valid_map);
	if (!exist && type == DATA_GENERIC_ENHANCE) 
	{
		f2fs_err(sbi, L"Inconsistent error blkaddr:%u, sit bitmap:%d", blkaddr, exist);
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		WARN_ON(1);
	}
	return exist;
}

bool f2fs_sb_info::f2fs_is_valid_blkaddr(block_t blkaddr, int type)
{
	switch (type) 
	{
	case META_NAT:	break;
	case META_SIT:	if (unlikely(blkaddr >= SIT_BLK_CNT(this)))			return false;		break;
	case META_SSA:	if (unlikely(blkaddr >= MAIN_BLKADDR(this) || blkaddr < SM_I()->ssa_blkaddr))	return false;
		break;
	case META_CP:	if (unlikely(blkaddr >= SIT_I()->sit_base_addr || blkaddr < __start_cp_addr()))	return false;
		break;
	case META_POR:	if (unlikely(blkaddr >= MAX_BLKADDR(this) || blkaddr < MAIN_BLKADDR(this)))		return false;
		break;
	case DATA_GENERIC:
	case DATA_GENERIC_ENHANCE:
	case DATA_GENERIC_ENHANCE_READ:
		if (unlikely(blkaddr >= MAX_BLKADDR(this) || blkaddr < MAIN_BLKADDR(this))) 
		{
//			f2fs_warn(this, L"access invalid blkaddr:%u", blkaddr);
			LOG_WARNING(L"[warning] access invalid blkaddr:%u", blkaddr);
			set_sbi_flag(SBI_NEED_FSCK);
//			WARN_ON(1);
			return false;
		} 
		else {			return __is_bitmap_valid(this, blkaddr, type);		}
		break;
	case META_GENERIC:
		if (unlikely(blkaddr < SEG0_BLKADDR(this) || blkaddr >= MAIN_BLKADDR(this)))	return false;
		break;
	default:
//		BUG();
		JCASSERT(0);
	}
	return true;
}

/* Readahead CP/NAT/SIT/SSA/POR pages */
int f2fs_sb_info::f2fs_ra_meta_pages(block_t start, int nrpages, int type, bool sync)
{
	LOG_DEBUG(L"blk=%u, page no=%d type=%d", start, nrpages, type);
	struct page *page;
	block_t blkno = start;
	
	f2fs_io_info fio;// = {
	fio.sbi = this;
	fio.type = META;
	fio.op = REQ_OP_READ;
	fio.op_flags = sync ? (REQ_META | REQ_PRIO) : REQ_RAHEAD;
	fio.encrypted_page = NULL;
	fio.in_list = false;
	fio.is_por = (type == META_POR);
	//};
	blk_plug plug;
	int err;

	if (unlikely(type == META_POR))		fio.op_flags &= ~REQ_META;

	blk_start_plug(&plug);
	for (; nrpages-- > 0; blkno++)
	{
		if (!f2fs_is_valid_blkaddr(blkno, type))			goto out;

		switch (type) 
		{
		case META_NAT:
			if (unlikely(blkno >= NAT_BLOCK_OFFSET(NM_I(this)->max_nid)))		blkno = 0;
			/* get nat block addr */
			fio.new_blkaddr = current_nat_addr(this, blkno * NAT_ENTRY_PER_BLOCK);
			break;
		case META_SIT:
			if (unlikely(blkno >= TOTAL_SEGS(this)))
			{
				LOG_NOTICE(L"no more SIT blocks. blkno=%d, total segs=%d", blkno, sm_info->segment_count);
				goto out;
			}
			/* get sit block addr */
			fio.new_blkaddr = current_sit_addr(blkno * SIT_ENTRY_PER_BLOCK);
			break;
		case META_SSA:
		case META_CP:
		case META_POR:
			fio.new_blkaddr = blkno;
			break;
		default:
			JCASSERT(0);
		}

		page = f2fs_grab_cache_page(META_MAPPING(this),	fio.new_blkaddr, false);
		if (!page)			continue;
#ifdef _DEBUG
		jcvos::Utf8ToUnicode(page->m_type, "meta");
		LOG_DEBUG(L"new page, page=%llX, addr=%llX, type=%s, index=%d", 
			page, page->virtual_add, page->m_type.c_str(), page->index);
#endif
		if (PageUptodate(page)) 
		{
			f2fs_put_page(page, 1);
			continue;
		}

		fio.page = page;
		err = f2fs_submit_page_bio(&fio);
		if (err) 		{ 			LOG_ERROR(L"[err] failed on submitting page bio, err=%d", err);		}
		f2fs_put_page(page, err ? 1 : 0);
		if (!err)		f2fs_update_iostat(this, FS_META_READ_IO, F2FS_BLKSIZE);
	}
out:
	blk_finish_plug(&plug);
	return blkno - start;
}

void f2fs_ra_meta_pages_cond(struct f2fs_sb_info *sbi, pgoff_t index)
{
	struct page *page;
	bool readahead = false;

	page = find_get_page(META_MAPPING(sbi), index);
	if (!page || !PageUptodate(page))
		readahead = true;
	f2fs_put_page(page, 0);

	if (readahead)
		sbi->f2fs_ra_meta_pages( index, BIO_MAX_VECS, META_POR, true);
}

//static int __f2fs_write_meta_page(struct page *ppage, struct writeback_control *wbc, enum iostat_type io_type)
int f2fs_sb_info::__f2fs_write_meta_page(page* ppage, writeback_control* wbc, enum iostat_type io_type)
{
	LOG_DEBUG(L"write meta page: index=0x%X", ppage->index);
//	struct f2fs_sb_info *sbi = F2FS_P_SB(ppage);
	//trace_f2fs_writepage(ppage, META);

	if (unlikely(this->f2fs_cp_error()))
		goto redirty_out;
	if (unlikely(this->is_sbi_flag_set( SBI_POR_DOING)))
		goto redirty_out;
	if (wbc->for_reclaim && ppage->index < GET_SUM_BLOCK(this, 0))
		goto redirty_out;

	f2fs_do_write_meta_page(ppage, io_type);
	this->dec_page_count(F2FS_DIRTY_META);

	if (wbc->for_reclaim)
		f2fs_submit_merged_write_cond(this, NULL, ppage, 0, META);

	unlock_page(ppage);

	if (unlikely(this->f2fs_cp_error()))
		f2fs_submit_merged_write(this, META);

	return 0;

redirty_out:
#if 0 //TODO
	redirty_page_for_writepage(wbc, ppage);
#else
	JCASSERT(0);
#endif
	return AOP_WRITEPAGE_ACTIVATE;
}

//static int f2fs_write_meta_page(struct page *page, struct writeback_control *wbc)
int Cf2fsMetaMapping::write_page(page * page, writeback_control *wbc)
{
#if 0
	return __f2fs_write_meta_page(page, wbc, FS_META_IO);
#else
	JCASSERT(0);
	return -1;
#endif
}
//static int f2fs_write_meta_pages(address_space *mapping, struct writeback_control *wbc)
int Cf2fsMetaMapping::write_pages(writeback_control * wbc)
{
#if 1
//	struct f2fs_sb_info *sbi = F2FS_M_SB(mapping);
	f2fs_sb_info* sbi = dynamic_cast<f2fs_sb_info*>(host->i_sb);
	long diff, written;

	if (unlikely(sbi->is_sbi_flag_set( SBI_POR_DOING)))
		goto skip_write;

	/* collect a number of dirty meta pages and write together */
	if (wbc->sync_mode != WB_SYNC_ALL && sbi->get_pages( F2FS_DIRTY_META) < nr_pages_to_skip(sbi, META))
		goto skip_write;

	/* if locked failed, cp will flush dirty pages instead */
	if (!down_write_trylock(&sbi->cp_global_sem))
		goto skip_write;

//	trace_f2fs_writepages(mapping->host, wbc, META);
	diff = nr_pages_to_write(sbi, META, wbc);
	written = sbi->f2fs_sync_meta_pages(META, wbc->nr_to_write, FS_META_IO);
	up_write(&sbi->cp_global_sem);
	wbc->nr_to_write = max((long)0, wbc->nr_to_write - written - diff);
	return 0;

skip_write:
	wbc->pages_skipped += sbi->get_pages( F2FS_DIRTY_META);
//	trace_f2fs_writepages(mapping->host, wbc, META);
#else
	JCASSERT(0);
#endif
	return 0;
}

//long f2fs_sync_meta_pages(f2fs_sb_info *sbi, enum page_type type, long nr_to_write, enum iostat_type io_type)
long f2fs_sb_info::f2fs_sync_meta_pages(enum page_type type, long nr_to_write, enum iostat_type io_type)
{
	address_space *mapping = META_MAPPING(this);
	pgoff_t index = 0, prev = ULONG_MAX;
	pagevec pvec;
	long nwritten = 0;
	int nr_pages;
	writeback_control wbc;
	wbc.for_reclaim = 0;
	blk_plug plug;

	pagevec_init(&pvec);

	blk_start_plug(&plug);

	while ((nr_pages = pagevec_lookup_tag(&pvec, mapping, &index, PAGECACHE_TAG_DIRTY))) 
	{
		int i;

		for (i = 0; i < nr_pages; i++) 
		{
			page *ppage = pvec.pages[i];
			LOG_DEBUG(L"check meta page, page=%p, index=%d", ppage, ppage->index);

			if (prev == ULONG_MAX)			prev = ppage->index - 1;
			if (nr_to_write != LONG_MAX && ppage->index != prev + 1) 
			{
				pagevec_release(&pvec);
				goto stop;
			}

			//auto_lock<page_auto_lock> page_locker(*ppage);
			auto_lock_<page> page_locker(*ppage);
//			lock_page(ppage);

			if (unlikely(ppage->mapping != mapping)) 
			{
//continue_unlock:
//				unlock_page(ppage);
				continue;
			}
			if (!PageDirty(ppage)) 
			{
				/* someone wrote it for us */
//				goto continue_unlock;
//				unlock_page(ppage);
				continue;
			}
			LOG_DEBUG(L"page is durity");

			f2fs_wait_on_page_writeback(ppage, META, true, true);

			if (!clear_page_dirty_for_io(ppage))
			{
//				unlock_page(ppage);
				continue;
//				goto continue_unlock;
			}

			if (__f2fs_write_meta_page(ppage, &wbc, io_type)) 
			{
//				unlock_page(ppage);
				break;
			}
			page_locker.keep_lock();
			nwritten++;
			prev = ppage->index;
			if (unlikely(nwritten >= nr_to_write))		break;
		}
		pagevec_release(&pvec);
//		cond_resched();
	}
stop:
	if (nwritten)
		f2fs_submit_merged_write(this, type);

	blk_finish_plug(&plug);

	return nwritten;
}


//static int f2fs_set_meta_page_dirty(struct page *page)
int Cf2fsMetaMapping::set_node_page_dirty(page * page)
{
//	trace_f2fs_set_page_dirty(page, META);
	if (!PageUptodate(page))		SetPageUptodate(page);
	if (!PageDirty(page)) 
	{
		__set_page_dirty_nobuffers(page);
		F2FS_P_SB(page)->inc_page_count( F2FS_DIRTY_META);
		f2fs_set_page_private(page, 0);
		return 1;
	}
	return 0;
}

//const struct address_space_operations f2fs_meta_aops = {
//	.writepage	= f2fs_write_meta_page,
//	.writepages	= f2fs_write_meta_pages,
//	.set_page_dirty	= f2fs_set_meta_page_dirty,
//	.invalidatepage = f2fs_invalidate_page,			=> Cf2fsMappingBase;
//	.releasepage	= f2fs_release_page,
//#ifdef CONFIG_MIGRATION
//	.migratepage    = f2fs_migrate_page,
//#endif
//};

//static void __add_ino_entry(f2fs_sb_info* sbi, nid_t ino, unsigned int devidx, int type)
void f2fs_sb_info::__add_ino_entry(nid_t ino, unsigned int devidx, int type)
{
	inode_management *im = &this->im[type];
	ino_entry *e, *tmp;

	tmp = f2fs_kmem_cache_alloc<ino_entry>(ino_entry_slab, GFP_NOFS);

	radix_tree_preload(GFP_NOFS | __GFP_NOFAIL);

	spin_lock(&im->ino_lock);
	e = radix_tree_lookup<ino_entry>(&im->ino_root, ino);
	if (!e)
	{
		e = tmp;
		if (unlikely(radix_tree_insert(&im->ino_root, ino, e)))
			f2fs_bug_on(this, 1);

		memset(e, 0, sizeof(struct ino_entry));
		e->ino = ino;

		::list_add_tail(&e->list, &im->ino_list);
		if (type != ORPHAN_INO)
			im->ino_num++;
	}

	if (type == FLUSH_INO)
		f2fs_set_bit(devidx, (char *)&e->dirty_device);

	spin_unlock(&im->ino_lock);
	radix_tree_preload_end();

	if (e != tmp)
		kmem_cache_free(ino_entry_slab, tmp);
}


static void __remove_ino_entry(f2fs_sb_info *sbi, nid_t ino, int type)
{
	inode_management *im = &sbi->im[type];
	ino_entry *e;

	spin_lock(&im->ino_lock);
	e = radix_tree_lookup<ino_entry>(&im->ino_root, ino);
	if (e) 
	{
		list_del(&e->list);
		radix_tree_delete<ino_entry>(&im->ino_root, ino);
		im->ino_num--;
		spin_unlock(&im->ino_lock);
		kmem_cache_free(ino_entry_slab, e);
		return;
	}
	spin_unlock(&im->ino_lock);
}

void f2fs_add_ino_entry(f2fs_sb_info *sbi, nid_t ino, int type)
{
	/* add new dirty ino entry into list */
	sbi->__add_ino_entry(ino, 0, type);
}


void f2fs_remove_ino_entry(f2fs_sb_info *sbi, nid_t ino, int type)
{
	/* remove dirty ino entry from list */
	__remove_ino_entry(sbi, ino, type);
}

/* mode should be APPEND_INO, UPDATE_INO or TRANS_DIR_INO */
bool f2fs_exist_written_data(f2fs_sb_info *sbi, nid_t ino, int mode)
{
	inode_management *im = &sbi->im[mode];
	ino_entry *e;

	spin_lock(&im->ino_lock);
	e = radix_tree_lookup<ino_entry>(&im->ino_root, ino);
	spin_unlock(&im->ino_lock);
	return e ? true : false;
}

void f2fs_release_ino_entry(struct f2fs_sb_info *sbi, bool all)
{
	struct ino_entry *e, *tmp;
	int i;

	for (i = all ? ORPHAN_INO : APPEND_INO; i < MAX_INO_ENTRY; i++) {
		struct inode_management *im = &sbi->im[i];

		spin_lock(&im->ino_lock);
		list_for_each_entry_safe(ino_entry, e, tmp, &im->ino_list, list) 
		{
			list_del(&e->list);
			radix_tree_delete<ino_entry>(&im->ino_root, e->ino);
			kmem_cache_free(ino_entry_slab, e);
			im->ino_num--;
		}
		spin_unlock(&im->ino_lock);
	}
}

void f2fs_set_dirty_device(f2fs_sb_info *sbi, nid_t ino, unsigned int devidx, int type)
{
	sbi->__add_ino_entry(ino, devidx, type);
}


bool f2fs_is_dirty_device(struct f2fs_sb_info *sbi, nid_t ino, unsigned int devidx, int type)
{
	struct inode_management *im = &sbi->im[type];
	bool is_dirty = false;

	spin_lock(&im->ino_lock);
	ino_entry * e = radix_tree_lookup<ino_entry>(&im->ino_root, ino);
	if (e && f2fs_test_bit(devidx, (char *)&e->dirty_device))		is_dirty = true;
	spin_unlock(&im->ino_lock);
	return is_dirty;
}


//int f2fs_acquire_orphan_inode(struct f2fs_sb_info *sbi)
int f2fs_sb_info::f2fs_acquire_orphan_inode(void)
{
	LOG_STACK_TRACE();
	inode_management *im = &this->im[ORPHAN_INO];
	int err = 0;

	spin_lock(&im->ino_lock);

	if (time_to_inject(this, FAULT_ORPHAN)) 
	{
		spin_unlock(&im->ino_lock);
		f2fs_show_injection_info(this, FAULT_ORPHAN);
		return -ENOSPC;
	}

	if (unlikely(im->ino_num >= this->max_orphans))		err = -ENOSPC;
	else		im->ino_num++;
	spin_unlock(&im->ino_lock);
	return err;
}

//void f2fs_release_orphan_inode(f2fs_sb_info *sbi)
void f2fs_sb_info::f2fs_release_orphan_inode(void)
{
	inode_management *im = &this->im[ORPHAN_INO];
	spin_lock(&im->ino_lock);
	f2fs_bug_on(this, im->ino_num == 0);
	im->ino_num--;
	spin_unlock(&im->ino_lock);
	LOG_DEBUG(L"[orphan track] release orphan node, remain=%d", im->ino_num);
}

//void f2fs_add_orphan_inode(f2fs_inode_info *iinode)
void f2fs_sb_info::f2fs_add_orphan_inode(f2fs_inode_info* iinode)
{
	LOG_DEBUG(L"[orphan track] add orphan node, ino=%d", iinode->i_ino);
	/* add new orphan ino entry into list */
	__add_ino_entry(iinode->i_ino, 0, ORPHAN_INO);
	iinode->f2fs_update_inode_page();
}

void f2fs_remove_orphan_inode(f2fs_sb_info *sbi, nid_t ino)
{
	/* remove orphan entry from orphan list */
	LOG_DEBUG(L"[orphan track] remove orphan node, ino=%d", ino);
	__remove_ino_entry(sbi, ino, ORPHAN_INO);
}

//static int recover_orphan_inode(f2fs_sb_info *sbi, nid_t ino)
int f2fs_sb_info::recover_orphan_inode(nid_t ino)
{
	f2fs_inode_info *iinode;
	node_info ni;
	int err;

	iinode = f2fs_iget_retry(ino);
	if (IS_ERR(iinode)) 
	{	/* there should be a bug that we can't find the entry to orphan inode. */
		f2fs_bug_on(this, PTR_ERR(iinode) == -ENOENT);
		return (int)PTR_ERR(iinode);
	}

#if 0 // <NOT SUPPORT>
	err = dquot_initialize(iinode);
	if (err) 
	{
		iput(iinode);
		goto err_out;
	}
#endif

	iinode->clear_nlink();

	/* truncate all the data during iput */
	iput(iinode);

	err = nm_info->f2fs_get_node_info( ino, &ni);
	if (err)	goto err_out;

	/* ENOMEM was fully retried in f2fs_evict_inode. */
	if (ni.blk_addr != NULL_ADDR) 
	{
		err = -EIO;
		goto err_out;
	}
	return 0;

err_out:
	set_sbi_flag(SBI_NEED_FSCK);
	LOG_WARNING(L"[warning] orphan failed (ino=%x), run fsck to fix.",  ino);
	return err;
}

//int f2fs_recover_orphan_inodes(struct f2fs_sb_info *sbi)
int f2fs_sb_info::f2fs_recover_orphan_inodes(void)
{
	LOG_STACK_TRACE();
	block_t start_blk, orphan_blocks, i, j;
	unsigned int s_flags = this->s_flags;
	int err = 0;
#ifdef CONFIG_QUOTA
	int quota_enabled;
#endif

	if (!this->is_set_ckpt_flags(CP_ORPHAN_PRESENT_FLAG))	return 0;

	if (bdev_read_only(s_bdev))
	{
		LOG_NOTICE(L"write access unavailable, skipping orphan cleanup");
		return 0;
	}

	if (s_flags & SB_RDONLY)
	{
		LOG_NOTICE(L"orphan cleanup on readonly fs");
		s_flags &= ~SB_RDONLY;
	}

#ifdef CONFIG_QUOTA
	/* Needed for iput() to work correctly and not trash data */
	this->s_flags |= SB_ACTIVE;

	/* Turn on quotas which were not enabled for read-only mounts if filesystem has quota feature, so that they are updated correctly. */
	quota_enabled = f2fs_enable_quota_files(this, s_flags & SB_RDONLY);
#endif

	start_blk = __start_cp_addr() + 1 + __cp_payload();
	orphan_blocks = __start_sum_addr(this) - 1 - __cp_payload();

	this->f2fs_ra_meta_pages( start_blk, orphan_blocks, META_CP, true);

	for (i = 0; i < orphan_blocks; i++)
	{
		page * ppage = f2fs_get_meta_page(start_blk + i);
		if (IS_ERR(ppage))
		{
			err = (int)PTR_ERR(ppage);
			goto out;
		}

		f2fs_orphan_block * orphan_blk = page_address<f2fs_orphan_block>(ppage);
		for (j = 0; j < le32_to_cpu(orphan_blk->entry_count); j++)
		{
			nid_t ino = le32_to_cpu(orphan_blk->ino[j]);
			LOG_DEBUG(L"get orphan node=%d", ino);
			err = recover_orphan_inode(ino);
			if (err)
			{
				f2fs_put_page(ppage, 1);
				goto out;
			}
		}
		f2fs_put_page(ppage, 1);
	}
	/* clear Orphan Flag */
	clear_ckpt_flags(this, CP_ORPHAN_PRESENT_FLAG);
out:
	set_sbi_flag(SBI_IS_RECOVERED);

#ifdef CONFIG_QUOTA
	/* Turn quotas off */
	if (quota_enabled)	f2fs_quota_off_umount(this->sb);
#endif
	s_flags = s_flags; /* Restore SB_RDONLY status */

	return err;
}

//static void write_orphan_inodes(struct f2fs_sb_info *sbi, block_t start_blk)
void f2fs_sb_info::write_orphan_inodes(block_t start_blk)
{
	LOG_STACK_TRACE();
	f2fs_orphan_block *orphan_blk = NULL;
	unsigned int nentries = 0;
	unsigned short index = 1;
	unsigned short orphan_blocks;
	page *ppage = NULL;
	ino_entry *orphan = NULL;
	inode_management *im = &this->im[ORPHAN_INO];

	orphan_blocks = boost::numeric_cast<unsigned short>( GET_ORPHAN_BLOCKS(im->ino_num) );

	/* we don't need to do spin_lock(&im->ino_lock) here, since all the orphan inode operations are covered under f2fs_lock_op(). And, spin_lock should be avoided due to page operations below. */
	list_head *head = &im->ino_list;

	/* loop for each orphan inode entry and write them in Jornal block */
	list_for_each_entry(ino_entry, orphan, head, list) 
	{
		if (!ppage)
		{
			ppage = f2fs_grab_meta_page(this, start_blk++);
			orphan_blk = page_address<f2fs_orphan_block>(ppage);
			memset(orphan_blk, 0, sizeof(*orphan_blk));
		}
		LOG_DEBUG_(1, L"add orphan node=%d", orphan->ino);
		orphan_blk->ino[nentries++] = cpu_to_le32(orphan->ino);

		if (nentries == F2FS_ORPHANS_PER_BLOCK) 
		{
			/* an orphan block is full of 1020 entries, then we need to flush current orphan blocks and bring another one in memory */
			orphan_blk->blk_addr = cpu_to_le16(index);
			orphan_blk->blk_count = cpu_to_le16(orphan_blocks);
			orphan_blk->entry_count = cpu_to_le32(nentries);
			set_page_dirty(ppage);
			f2fs_put_page(ppage, 1);
			index++;
			nentries = 0;
			ppage = NULL;
		}
	}

	if (ppage) 
	{
		orphan_blk->blk_addr = cpu_to_le16(index);
		orphan_blk->blk_count = cpu_to_le16(orphan_blocks);
		orphan_blk->entry_count = cpu_to_le32(nentries);
		set_page_dirty(ppage);
		f2fs_put_page(ppage, 1);
	}
}


//<YUAN> move to libf2fs.cpp f2fs_checkpoint_checksum
//static __u32 f2fs_checkpoint_chksum(f2fs_sb_info *sbi, f2fs_checkpoint *ckpt)
//{
//	unsigned int chksum_ofs = le32_to_cpu(ckpt->checksum_offset);
//	__u32 chksum;
//
//	chksum = f2fs_crc32(sbi, ckpt, chksum_ofs);
//	if (chksum_ofs < CP_CHKSUM_OFFSET)
//	{
//		chksum_ofs += sizeof(chksum);
//		chksum = f2fs_chksum(sbi, chksum, (__u8 *)ckpt + chksum_ofs, F2FS_BLKSIZE - chksum_ofs);
//	}
//	return chksum;
//}

int f2fs_sb_info::get_checkpoint_version(block_t cp_addr, f2fs_checkpoint **cp_block, page **cp_page, unsigned long long *version)
{
	size_t crc_offset = 0;
	__u32 crc;

	//*cp_page = f2fs_get_meta_page(m_sb_info, cp_addr);
	*cp_page = __get_meta_page(cp_addr, true);
	if (IS_ERR(*cp_page))
	{
		LOG_ERROR(L"[err] failed on getting meta page");
		return -EINVAL;//PTR_ERR(*cp_page);
	}

	*cp_block = page_address<f2fs_checkpoint>(*cp_page);

	crc_offset = le32_to_cpu((*cp_block)->checksum_offset);
	if (crc_offset < CP_MIN_CHKSUM_OFFSET || crc_offset > CP_CHKSUM_OFFSET) 
	{
		f2fs_put_page(*cp_page, 1);
		f2fs_warn(sbi, L"[warn] invalid crc_offset: %zu", crc_offset);
		return -EINVAL;
	}

	crc = f2fs_checkpoint_chksum(*cp_block);
	if (crc != cur_cp_crc(*cp_block)) 
	{
		f2fs_put_page(*cp_page, 1);
		f2fs_warn(sbi, L"[warn] invalid crc value", 0);
		return -EINVAL;
	}

	*version = cur_cp_version(*cp_block);
	return 0;
}


page * f2fs_sb_info::validate_checkpoint(block_t cp_addr, unsigned long long *version)
{
	page *cp_page_1 = NULL, *cp_page_2 = NULL;
	f2fs_checkpoint *cp_block = NULL;
	unsigned long long cur_version = 0, pre_version = 0;
	int err;

	err = get_checkpoint_version(cp_addr, &cp_block, &cp_page_1, version);
	if (err) return NULL;

	if (le32_to_cpu(cp_block->cp_pack_total_block_count) > blocks_per_seg) 
	{
		f2fs_warn(sbi, L"[warn] invalid cp_pack_total_block_count:%u", le32_to_cpu(cp_block->cp_pack_total_block_count));
		goto invalid_cp;
	}
	pre_version = *version;

	cp_addr += le32_to_cpu(cp_block->cp_pack_total_block_count) - 1;
	err = get_checkpoint_version(cp_addr, &cp_block, &cp_page_2, version);
	if (err) goto invalid_cp;
	cur_version = *version;

	if (cur_version == pre_version) 
	{
		*version = cur_version;
		f2fs_put_page(cp_page_2, 1);
		return cp_page_1;
	}
	f2fs_put_page(cp_page_2, 1);
invalid_cp:
	f2fs_put_page(cp_page_1, 1);
	return NULL;
}


int f2fs_sb_info::f2fs_get_valid_checkpoint(void)
{
	f2fs_checkpoint *cp_block;
	f2fs_super_block *fsb = raw_super;
	page *cp1, *cp2, *cur_page;
	unsigned long blk_size = blocksize;
	unsigned long long cp1_version = 0, cp2_version = 0;
	unsigned int cp_blks = 1 + __cp_payload();
	block_t cp_blk_no;
	int err;

	UINT cp_size = blk_size * cp_blks;
	LOG_DEBUG(L"allocate raw checkpoint, blocks=%d, payload size=%d, f2fs_checkpoint size=%d", 
		cp_blks, cp_size, sizeof(f2fs_checkpoint));
	ckpt = (f2fs_checkpoint*)malloc(cp_size);		// 在f2fs_sb_info::put_super中删除
	if (!ckpt)		return -ENOMEM;
	/* Finding out valid cp block involves read both sets( cp pack 1 and cp pack 2)	 */
	UINT32 cp_start_blk_no = le32_to_cpu(fsb->cp_blkaddr);
	cp1 = validate_checkpoint(cp_start_blk_no, &cp1_version);

	/* The second checkpoint pack should start at the next segment */
	cp_start_blk_no += ((UINT32)1) << le32_to_cpu(fsb->log_blocks_per_seg);
	cp2 = validate_checkpoint(cp_start_blk_no, &cp2_version);
	LOG_DEBUG(L"read cp, cp1:page_index=0x%X, ver=%d, cp2:page_index=0x%X, ver=%d", 
		cp1->index, cp1_version, cp2->index, cp2_version);
	if (cp1 && cp2) 
	{
		if (ver_after(cp2_version, cp1_version))	cur_page = cp2;
		else										cur_page = cp1;
	} 
	else if (cp1) 	{		cur_page = cp1;	} 
	else if (cp2)	{		cur_page = cp2;	}
	else 
	{
		LOG_ERROR(L"[err] either cp1 or cp2 are not available.");
		err = -EFSCORRUPTED;
		goto fail_no_cp;
	}

	cp_block = page_address<f2fs_checkpoint>(cur_page);
	memcpy_s(ckpt, cp_size, cp_block, blk_size);	//

	if (cur_page == cp1)	cur_cp_pack = 1;
	else					cur_cp_pack = 2;
	/* Sanity checking of checkpoint */
	if (f2fs_sanity_check_ckpt(this))
	{
		err = -EFSCORRUPTED;
		goto free_fail_no_cp;
	}

	if (cp_blks <= 1)		goto done;

	cp_blk_no = le32_to_cpu(fsb->cp_blkaddr);
	if (cur_page == cp2)		cp_blk_no += 1 << le32_to_cpu(fsb->log_blocks_per_seg);

	// 读取后续checkpoint数据
	for (UINT i = 1; i < cp_blks; i++) 
	{
		void *sit_bitmap_ptr;
		unsigned char *_ckpt = (unsigned char *)ckpt;
		LOG_DEBUG(L"read meta data, blk=0x%X", cp_blk_no + i);
		cur_page = f2fs_get_meta_page(cp_blk_no + i);
		if (IS_ERR(cur_page))
		{
			LOG_ERROR(L"[err] failed on getting meta page");
			err = -EINVAL; //PTR_ERR(cur_page);
			goto free_fail_no_cp;
		}
		sit_bitmap_ptr = page_address<void>(cur_page);
//		memcpy(ckpt + i * blk_size, sit_bitmap_ptr, blk_size);
		memcpy_s(_ckpt + i * blk_size, (cp_blks - i) * blk_size, sit_bitmap_ptr, blk_size);
		f2fs_put_page(cur_page, 1);
	}
done:
	f2fs_put_page(cp1, 1);
	f2fs_put_page(cp2, 1);
	return 0;

free_fail_no_cp:
	f2fs_put_page(cp1, 1);
	f2fs_put_page(cp2, 1);
fail_no_cp:
	//f2fs_kvfree(ckpt);
	free(ckpt);
	ckpt = NULL;
	return err;
}


static void __add_dirty_inode(f2fs_inode_info*iinode, inode_type type)
{
	f2fs_sb_info *sbi = F2FS_I_SB(iinode);
//	f2fs_inode_info* finode = dynamic_cast<f2fs_inode_info*>(iinode);
	int flag = (type == DIR_INODE) ? FI_DIRTY_DIR : FI_DIRTY_FILE;

	if (is_inode_flag_set(iinode, flag))	return;

	F2FS_I(iinode)->set_inode_flag(flag);
	if (!f2fs_is_volatile_file(iinode))
	{
//		list_add_tail(&F2FS_I(iinode)->dirty_list, &sbi->inode_list[type]);
		F_LOG_DEBUG(L"inode", L" add=%p, ino=%d, type=%d - add to sb iinode list", iinode, iinode->i_ino, type);
		sbi->sb_list_add_tail(iinode, type);
	}
	stat_inc_dirty_inode(sbi, type);
}


static void __remove_dirty_inode(f2fs_inode_info*iinode, enum inode_type type)
{
	f2fs_sb_info* sbi = iinode->m_sbi;
//	f2fs_inode_info* finode = dynamic_cast<f2fs_inode_info*>(iinode);

	int flag = (type == DIR_INODE) ? FI_DIRTY_DIR : FI_DIRTY_FILE;
	if (get_dirty_pages(iinode) || !is_inode_flag_set(iinode, flag))		return;
	//list_del_init(&F2FS_I(iinode)->dirty_list);
	F_LOG_DEBUG(L"inode", L" add=%p, ino=%d, type=%d - add to sb inode list", iinode, iinode->i_ino, type);
	sbi->sb_list_del_init(iinode, type);
	clear_inode_flag(iinode, flag);
	stat_dec_dirty_inode(sbi, type);
}

void f2fs_update_dirty_page(f2fs_inode_info*inode, struct page *page)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	enum inode_type type = S_ISDIR(inode->i_mode) ? DIR_INODE : FILE_INODE;

	if (!S_ISDIR(inode->i_mode) && !S_ISREG(inode->i_mode) && !S_ISLNK(inode->i_mode))
		return;

	spin_lock(&sbi->inode_lock[type]);
	if (type != FILE_INODE || test_opt(sbi, DATA_FLUSH))
		__add_dirty_inode(inode, type);
	F_LOG_DEBUG(L"page.dirty", L" inc: inode=%d, page=%d", inode->i_ino, page->index);
	inode_inc_dirty_pages(inode);
	spin_unlock(&sbi->inode_lock[type]);

	f2fs_set_page_private(page, 0);
}


void f2fs_remove_dirty_inode(f2fs_inode_info*inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	enum inode_type type = S_ISDIR(inode->i_mode) ? DIR_INODE : FILE_INODE;

	if (!S_ISDIR(inode->i_mode) && !S_ISREG(inode->i_mode) && !S_ISLNK(inode->i_mode))
		return;

	if (type == FILE_INODE && !test_opt(sbi, DATA_FLUSH))
		return;

	spin_lock(&sbi->inode_lock[type]);
	__remove_dirty_inode(inode, type);
	spin_unlock(&sbi->inode_lock[type]);
}

int f2fs_sync_dirty_inodes(f2fs_sb_info* sbi, enum inode_type type)
{
	//	list_head *head;
	//	inode *iinode;
	//	f2fs_inode_info *fi;
	bool is_dir = (type == DIR_INODE);
	unsigned long ino = 0;

	//trace_f2fs_sync_dirty_inodes_enter(sbi->sb, is_dir, sbi->get_pages( is_dir ?	F2FS_DIRTY_DENTS : F2FS_DIRTY_DATA));
//retry:
	while (1)
	{
		if (unlikely(sbi->f2fs_cp_error()))
		{
			//		trace_f2fs_sync_dirty_inodes_exit(sbi->sb, is_dir,	sbi->get_pages( is_dir ?	F2FS_DIRTY_DENTS : F2FS_DIRTY_DATA));
			return -EIO;
		}

		spin_lock(&sbi->inode_lock[type]);

		//	head = &sbi->inode_list[type];
		//	if (list_empty(head)) 
		if (sbi->list_empty(type))
		{
			spin_unlock(&sbi->inode_lock[type]);
			//		trace_f2fs_sync_dirty_inodes_exit(sbi->sb, is_dir,	sbi->get_pages( is_dir ? F2FS_DIRTY_DENTS : F2FS_DIRTY_DATA));
			return 0;
		}
		//fi = list_first_entry(head, f2fs_inode_info, dirty_list);
		f2fs_inode_info* fi = sbi->get_list_first_entry(type);
		inode* iinode = igrab(fi);
		spin_unlock(&sbi->inode_lock[type]);
		if (iinode)
		{
			unsigned long cur_ino = fi->i_ino;
			//		F2FS_I(iinode)->cp_task = current;
			//		fi->i_mapping->filemap_fdatawrite();
			fi->filemap_fdatawrite();
			//		fi->cp_task = NULL;

			iput(iinode);
			/* We need to give cpu to another writers. */
			if (ino == cur_ino)	/*	cond_resched()*/;
			else			ino = cur_ino;
		}
		else
		{
			/* We should submit bio, since it exists several wribacking dentry pages in the freeing inode. */
			f2fs_submit_merged_write(sbi, DATA);
			//		cond_resched();
		}
	}	// retry
//	goto retry;
}

int f2fs_sync_inode_meta(f2fs_sb_info *sbi)
{
//	list_head *head = &sbi->inode_list[DIRTY_META];
	f2fs_inode_info *fi;
	s64 total = sbi->get_pages( F2FS_DIRTY_IMETA);

	while (total--) 
	{
		if (unlikely(sbi->f2fs_cp_error()))
			return -EIO;

		spin_lock(&sbi->inode_lock[DIRTY_META]);
//		if (list_empty(head)) 
		if (sbi->list_empty(DIRTY_META))
		{
			spin_unlock(&sbi->inode_lock[DIRTY_META]);
			return 0;
		}
//		fi = list_first_entry(head, f2fs_inode_info, gdirty_list);
		fi = sbi->get_list_first_entry(DIRTY_META);
		F_LOG_DEBUG(L"inode", L" add=%p, ino=%d, - try to sync", fi, fi->i_ino);
		inode* iinode = igrab(fi);
		spin_unlock(&sbi->inode_lock[DIRTY_META]);
		if (iinode)
		{
//			inode * iinode = igrab(fi);
			sync_inode_metadata(iinode, 0);
			/* it's on eviction */
			if (is_inode_flag_set(fi, FI_DIRTY_INODE))		fi->f2fs_update_inode_page();
			iput(iinode);
		}
	}
	return 0;
}

static void __prepare_cp_block(struct f2fs_sb_info *sbi)
{
	struct f2fs_checkpoint *ckpt = sbi->F2FS_CKPT();
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	nid_t last_nid = nm_i->next_scan_nid;

	next_free_nid(sbi, &last_nid);
	ckpt->valid_block_count = cpu_to_le64(sbi->valid_user_blocks());
	ckpt->valid_node_count = cpu_to_le32(sbi->valid_node_count());
	LOG_DEBUG(L"save ckeckpoint, total_valid_node_count=%d", ckpt->valid_node_count);
	ckpt->valid_inode_count = cpu_to_le32(sbi->valid_inode_count());
	ckpt->next_free_nid = cpu_to_le32(last_nid);
}

static bool __need_flush_quota(struct f2fs_sb_info *sbi)
{
	bool ret = false;

	if (!is_journalled_quota(sbi))
		return false;

	down_write(&sbi->quota_sem);
	if (sbi->is_sbi_flag_set( SBI_QUOTA_SKIP_FLUSH)) {
		ret = false;
	} else if (sbi->is_sbi_flag_set( SBI_QUOTA_NEED_REPAIR)) {
		ret = false;
	} else if (sbi->is_sbi_flag_set( SBI_QUOTA_NEED_FLUSH)) {
		clear_sbi_flag(sbi, SBI_QUOTA_NEED_FLUSH);
		ret = true;
	} else if (sbi->get_pages( F2FS_DIRTY_QDATA)) {
		ret = true;
	}
	up_write(&sbi->quota_sem);
	return ret;
}

/* Freeze all the FS-operations for checkpoint. */
static int block_operations(struct f2fs_sb_info *sbi)
{
	struct writeback_control wbc;
	wbc.sync_mode = WB_SYNC_ALL;
	wbc.nr_to_write = LONG_MAX;
	wbc.for_reclaim = 0;
	
	int err = 0, cnt = 0;

	/* Let's flush inline_data in dirty node pages.	 */
	f2fs_flush_inline_data(sbi);

retry_flush_quotas:
	LOG_DEBUG(L"try to block all op")
	sbi->f2fs_lock_all();
	LOG_DEBUG(L"got block all op")
	if (__need_flush_quota(sbi)) 
	{
		int locked;

		if (++cnt > DEFAULT_RETRY_QUOTA_FLUSH_COUNT)
		{
			sbi->set_sbi_flag(SBI_QUOTA_SKIP_FLUSH);
			sbi->set_sbi_flag(SBI_QUOTA_NEED_FLUSH);
			goto retry_flush_dents;
		}
		sbi->f2fs_unlock_all();
		LOG_DEBUG(L"unblock all op");

		/* only failed during mount/umount/freeze/quotactl */
		locked = down_read_trylock(&sbi->s_umount);
		f2fs_quota_sync(sbi, -1);
		if (locked)			up_read(&sbi->s_umount);
		// cond_resched();
		goto retry_flush_quotas;
	}

retry_flush_dents:
	/* write all the dirty dentry pages */
	if (sbi->get_pages( F2FS_DIRTY_DENTS)) 
	{
		sbi->f2fs_unlock_all();
		LOG_DEBUG(L"unblock all op");
		err = f2fs_sync_dirty_inodes(sbi, DIR_INODE);
		if (err)			return err;
		// cond_resched();
		goto retry_flush_quotas;
	}

	/* POR: we should ensure that there are no dirty node pages until finishing nat/sit flush. inode->i_blocks can be updated. */
	down_write(&sbi->node_change);
	if (sbi->get_pages( F2FS_DIRTY_IMETA)) 
	{
		up_write(&sbi->node_change);
		sbi->f2fs_unlock_all();
		LOG_DEBUG(L"unblock all op");
		err = f2fs_sync_inode_meta(sbi);
		if (err)		return err;
		// cond_resched();
		goto retry_flush_quotas;
	}

retry_flush_nodes:
	down_write(&sbi->node_write);

	if (sbi->get_pages( F2FS_DIRTY_NODES)) {
		up_write(&sbi->node_write);
		atomic_inc(&sbi->wb_sync_req[NODE]);
		err = f2fs_sync_node_pages(sbi, &wbc, false, FS_CP_NODE_IO);
		atomic_dec(&sbi->wb_sync_req[NODE]);
		if (err)
		{
			up_write(&sbi->node_change);
			sbi->f2fs_unlock_all();
			LOG_DEBUG(L"unblock all op");
			return err;
		}
		//cond_resched();
		goto retry_flush_nodes;
	}

	/* sbi->node_change is used only for AIO write_begin path which produces dirty node blocks and some checkpoint 
	   values by block allocation. */
	__prepare_cp_block(sbi);
	up_write(&sbi->node_change);
	return err;
}

static void unblock_operations(struct f2fs_sb_info *sbi)
{
	up_write(&sbi->node_write);
	sbi->f2fs_unlock_all();
}

void ckpt_req_control::f2fs_wait_on_all_pages(/*f2fs_sb_info *sbi,*/ int type)
{
#if 1 //TODO
//	DEFINE_WAIT(wait);

	for (;;) 
	{
		LOG_DEBUG(L"pending page for %d = %d", type, m_sbi->get_pages(type));
		if (!m_sbi->get_pages(type))	break;
		if (unlikely(m_sbi->f2fs_cp_error()))		break;
		if (type == F2FS_DIRTY_META)			m_sbi->f2fs_sync_meta_pages(META, LONG_MAX, FS_CP_META_IO);
		else if (type == F2FS_WB_CP_DATA)		f2fs_submit_merged_write(m_sbi, DATA);

//		prepare_to_wait(&sbi->cp_wait, &wait, TASK_UNINTERRUPTIBLE);
//		io_schedule_timeout(DEFAULT_IO_TIMEOUT);
		DWORD ir = WaitForSingleObject(m_wait, DEFAULT_IO_TIMEOUT);
		if (ir != 0) LOG_WIN32_ERROR(L"[err] IO timeout");
	}
//	finish_wait(&sbi->cp_wait, &wait);
#else
	JCASSERT(0);
#endif
}


static void update_ckpt_flags(f2fs_sb_info *sbi, struct cp_control *cpc)
{
	unsigned long orphan_num = sbi->im[ORPHAN_INO].ino_num;
	struct f2fs_checkpoint *ckpt = sbi->F2FS_CKPT();
//	unsigned long flags;

	spin_lock_irqsave(&sbi->cp_lock, flags);

	if ((cpc->reason & CP_UMOUNT) && le32_to_cpu(ckpt->cp_pack_total_block_count) > sbi->blocks_per_seg - NM_I(sbi)->nat_bits_blocks)
	{
		NM_I(sbi)->disable_nat_bits(false);
	}

	if (cpc->reason & CP_TRIMMED)
		__set_ckpt_flags(ckpt, CP_TRIMMED_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_TRIMMED_FLAG);

	if (cpc->reason & CP_UMOUNT)
		__set_ckpt_flags(ckpt, CP_UMOUNT_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_UMOUNT_FLAG);

	if (cpc->reason & CP_FASTBOOT)
		__set_ckpt_flags(ckpt, CP_FASTBOOT_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_FASTBOOT_FLAG);

	if (orphan_num)
		__set_ckpt_flags(ckpt, CP_ORPHAN_PRESENT_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_ORPHAN_PRESENT_FLAG);

	if (sbi->is_sbi_flag_set( SBI_NEED_FSCK))
		__set_ckpt_flags(ckpt, CP_FSCK_FLAG);

	if (sbi->is_sbi_flag_set( SBI_IS_RESIZEFS))
		__set_ckpt_flags(ckpt, CP_RESIZEFS_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_RESIZEFS_FLAG);

	if (sbi->is_sbi_flag_set( SBI_CP_DISABLED))
		__set_ckpt_flags(ckpt, CP_DISABLED_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_DISABLED_FLAG);

	if (sbi->is_sbi_flag_set( SBI_CP_DISABLED_QUICK))
		__set_ckpt_flags(ckpt, CP_DISABLED_QUICK_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_DISABLED_QUICK_FLAG);

	if (sbi->is_sbi_flag_set( SBI_QUOTA_SKIP_FLUSH))
		__set_ckpt_flags(ckpt, CP_QUOTA_NEED_FSCK_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_QUOTA_NEED_FSCK_FLAG);

	if (sbi->is_sbi_flag_set( SBI_QUOTA_NEED_REPAIR))
		__set_ckpt_flags(ckpt, CP_QUOTA_NEED_FSCK_FLAG);

	/* set this flag to activate crc|cp_ver for recovery */
	__set_ckpt_flags(ckpt, CP_CRC_RECOVERY_FLAG);
	__clear_ckpt_flags(ckpt, CP_NOCRC_RECOVERY_FLAG);

	spin_unlock_irqrestore(&sbi->cp_lock, flags);
}

//static void commit_checkpoint(f2fs_sb_info *sbi, void *src, block_t blk_addr)
void f2fs_sb_info::commit_checkpoint(void* src, block_t blk_addr)
{
	LOG_DEBUG(L"commit checkpoint to block 0x%X", blk_addr);
	writeback_control wbc;
	wbc.for_reclaim = 0;

	/* pagevec_lookup_tag and lock_page again will take some extra time. Therefore, f2fs_update_meta_pages and f2fs_sync_meta_pages are combined in this function.	 */
	page *ppage = f2fs_grab_meta_page(this, blk_addr);
	int err;
	f2fs_wait_on_page_writeback(ppage, META, true, true);
	memcpy(page_address<void>(ppage), src, PAGE_SIZE);
	set_page_dirty(ppage);
	if (unlikely(!clear_page_dirty_for_io(ppage)))	f2fs_bug_on(this, 1);
	/* writeout cp pack 2 page */
	err = __f2fs_write_meta_page(ppage, &wbc, FS_CP_META_IO);
	if (unlikely(err && f2fs_cp_error())) 
	{
		f2fs_put_page(ppage, 1);
		return;
	}
	f2fs_bug_on(this, err);
	f2fs_put_page(ppage, 0);
	/* submit checkpoint (with barrier if NOBARRIER is not set) */
	f2fs_submit_merged_write(this, META_FLUSH);
}


static inline u64 get_sectors_written(block_device *bdev)
{
#if 0
	return (u64)part_stat_read(bdev, sectors[STAT_WRITE]);
#endif
	return 0;
}

u64 f2fs_get_sectors_written(struct f2fs_sb_info *sbi)
{
	if (sbi->f2fs_is_multi_device()) 
	{
		u64 sectors = 0;
		for (int i = 0; i < sbi->s_ndevs; i++)		sectors += get_sectors_written(FDEV(i).m_disk);
		return sectors;
	}
	return get_sectors_written(sbi->s_bdev);
}

//static int do_checkpoint(f2fs_sb_info *sbi, struct cp_control *cpc)
int f2fs_sb_info::do_checkpoint(cp_control* cpc)
{
//	struct f2fs_checkpoint *ckpt = sbi->F2FS_CKPT();
//	struct f2fs_nm_info *nm_i = NM_I(this);
	unsigned long orphan_num = im[ORPHAN_INO].ino_num;// , flags;
	block_t start_blk;
	unsigned int data_sum_blocks, orphan_blocks;
	__u32 crc32 = 0;
	int i;
	int cp_payload_blks = __cp_payload();
	curseg_info *seg_i = CURSEG_I( CURSEG_HOT_NODE);
	u64 kbytes_written;
	int err;
	LOG_DEBUG(L"cp blocks=%d", cp_payload_blks+1);

	/* Flush all the NAT/SIT pages */
	f2fs_sync_meta_pages(META, LONG_MAX, FS_CP_META_IO);

	/* start to update checkpoint, cp ver is already updated previously */
	ckpt->elapsed_time = cpu_to_le64(get_mtime(this, true));
	ckpt->free_segment_count = cpu_to_le32(free_segments());
	for (i = 0; i < NR_CURSEG_NODE_TYPE; i++)
	{
		ckpt->cur_node_segno[i] = cpu_to_le32(curseg_segno(this, i + CURSEG_HOT_NODE));
		ckpt->cur_node_blkoff[i] = cpu_to_le16(curseg_blkoff(i + CURSEG_HOT_NODE));
		ckpt->alloc_type[i + CURSEG_HOT_NODE] =	curseg_alloc_type(this, i + CURSEG_HOT_NODE);
	}
	for (i = 0; i < NR_CURSEG_DATA_TYPE; i++) 
	{
		ckpt->cur_data_segno[i] =	cpu_to_le32(curseg_segno(this, i + CURSEG_HOT_DATA));
		ckpt->cur_data_blkoff[i] =	cpu_to_le16(this->curseg_blkoff(i + CURSEG_HOT_DATA));
		ckpt->alloc_type[i + CURSEG_HOT_DATA] =		curseg_alloc_type(this, i + CURSEG_HOT_DATA);
	}

	/* 2 cp + n data seg summary + orphan inode blocks */
	data_sum_blocks = this->f2fs_npages_for_summary_flush( false);
	spin_lock_irqsave(&this->cp_lock, flags);
	if (data_sum_blocks < NR_CURSEG_DATA_TYPE)
		__set_ckpt_flags(ckpt, CP_COMPACT_SUM_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_COMPACT_SUM_FLAG);
	spin_unlock_irqrestore(&this->cp_lock, flags);

	orphan_blocks = GET_ORPHAN_BLOCKS(orphan_num);
	ckpt->cp_pack_start_sum = cpu_to_le32(1 + cp_payload_blks +	orphan_blocks);

	if (__remain_node_summaries(cpc->reason))
		ckpt->cp_pack_total_block_count = cpu_to_le32(F2FS_CP_PACKS +
				cp_payload_blks + data_sum_blocks +	orphan_blocks + NR_CURSEG_NODE_TYPE);
	else
		ckpt->cp_pack_total_block_count = cpu_to_le32(F2FS_CP_PACKS +
				cp_payload_blks + data_sum_blocks +	orphan_blocks);

	/* update ckpt flag for checkpoint */
	update_ckpt_flags(this, cpc);

	/* update SIT/NAT bitmap */
	get_sit_bitmap(__bitmap_ptr( SIT_BITMAP));
	get_nat_bitmap(this, __bitmap_ptr( NAT_BITMAP));

	crc32 = f2fs_checkpoint_chksum(ckpt);
	*((__le32 *)((unsigned char *)ckpt + le32_to_cpu(ckpt->checksum_offset)))	= cpu_to_le32(crc32);

	start_blk = __start_cp_next_addr(this);

	/* write nat bits */
	if (enabled_nat_bits(this, cpc)) 
	{
		__u64 cp_ver = cur_cp_version(ckpt);
		block_t blk;

		cp_ver |= ((__u64)crc32 << 32);
		*(__le64 *)nm_info->nat_bits = cpu_to_le64(cp_ver);

		blk = start_blk + this->blocks_per_seg - nm_info->nat_bits_blocks;
		for (unsigned int ii = 0; ii < nm_info->nat_bits_blocks; ii++)
			f2fs_update_meta_page(this, nm_info->nat_bits +	(ii << F2FS_BLKSIZE_BITS), blk + ii);
	}

	/* write out checkpoint buffer at block 0 */
	f2fs_update_meta_page(this, ckpt, start_blk++);

	for (i = 1; i < 1 + cp_payload_blks; i++)
		f2fs_update_meta_page(this, (char *)ckpt + i * F2FS_BLKSIZE, start_blk++);

	if (orphan_num)
	{
		write_orphan_inodes(start_blk);
		start_blk += orphan_blocks;
	}

	f2fs_write_data_summaries(this, start_blk);
	start_blk += data_sum_blocks;

	/* Record write statistics in the hot node summary */
	kbytes_written = this->kbytes_written;
	kbytes_written += (f2fs_get_sectors_written(this) -	this->sectors_written_start) >> 1;
	seg_i->journal.info.kbytes_written = cpu_to_le64(kbytes_written);

	if (__remain_node_summaries(cpc->reason)) 
	{
		f2fs_write_node_summaries(this, start_blk);
		start_blk += NR_CURSEG_NODE_TYPE;
	}

	/* update user_block_counts */
	this->last_valid_block_count = this->total_valid_block_count;
	percpu_counter_set(&this->alloc_valid_block_count, 0);

	/* Here, we have one bio having CP pack except cp pack 2 page */
	LOG_DEBUG(L"pending pages for DIRTY_META %d", this->get_pages(F2FS_DIRTY_META));
	f2fs_sync_meta_pages(META, LONG_MAX, FS_CP_META_IO);
	/* Wait for all dirty meta pages to be submitted for IO */
	LOG_DEBUG(L"pending pages for DIRTY_META %d", this->get_pages(F2FS_DIRTY_META));
	this->cprc_info.f2fs_wait_on_all_pages(F2FS_DIRTY_META);

	/* wait for previous submitted meta pages writeback */
	LOG_DEBUG(L"pending pages for WB_CP_DATA %d", this->get_pages(F2FS_WB_CP_DATA));
	this->cprc_info.f2fs_wait_on_all_pages(F2FS_WB_CP_DATA);

	/* flush all device cache */
	err = f2fs_flush_device_cache(this);
	if (err) return err;

	/* barrier and flush checkpoint cp pack 2 page if it can */
	commit_checkpoint(ckpt, start_blk);
	this->cprc_info.f2fs_wait_on_all_pages(F2FS_WB_CP_DATA);

	/* invalidate intermediate page cache borrowed from meta inode which are used for migration of encrypted, verity or compressed inode's blocks.	 */
	if (f2fs_sb_has_encrypt(this) || f2fs_sb_has_verity(this) ||f2fs_sb_has_compression(this))
		invalidate_mapping_pages(META_MAPPING(this), MAIN_BLKADDR(this), MAX_BLKADDR(this) - 1);

	f2fs_release_ino_entry(this, false);

	f2fs_reset_fsync_node_info(this);

	clear_sbi_flag(this, SBI_IS_DIRTY);
	clear_sbi_flag(this, SBI_NEED_CP);
	clear_sbi_flag(this, SBI_QUOTA_SKIP_FLUSH);

	spin_lock(&this->stat_lock);
	this->unusable_block_count = 0;
	spin_unlock(&this->stat_lock);

	__set_cp_next_pack(this);

	/* redirty superblock if metadata like node page or inode cache is updated during writing checkpoint.	 */
	if (this->get_pages( F2FS_DIRTY_NODES) || this->get_pages( F2FS_DIRTY_IMETA))
		this->set_sbi_flag(SBI_IS_DIRTY);

	f2fs_bug_on(this, this->get_pages( F2FS_DIRTY_DENTS));

	return unlikely(this->f2fs_cp_error()) ? -EIO : 0;
}

int f2fs_sb_info::f2fs_write_checkpoint(cp_control *cpc)
{
	f2fs_checkpoint *ckpt = F2FS_CKPT();
	unsigned long long ckpt_ver;
	int err = 0;

	if (f2fs_readonly() || f2fs_hw_is_readonly(this))		return -EROFS;

	if (unlikely(is_sbi_flag_set( SBI_CP_DISABLED)))
	{
		if (cpc->reason != CP_PAUSE)			return 0;
		LOG_WARNING(L"Start checkpoint disabled!");
	}
	if (cpc->reason != CP_RESIZE)		down_write(&cp_global_sem);

	if (!is_sbi_flag_set( SBI_IS_DIRTY) &&
		((cpc->reason & CP_FASTBOOT) || (cpc->reason & CP_SYNC) || ((cpc->reason & CP_DISCARD) && !discard_blks)))
		goto out;
	if (unlikely(f2fs_cp_error()))
	{
		err = -EIO;
		goto out;
	}

	//trace_f2fs_write_checkpoint(sb, cpc->reason, "start block_ops");

	err = block_operations(this);
	if (err) goto out;

	//trace_f2fs_write_checkpoint(sb, cpc->reason, "finish block_ops");

	f2fs_flush_merged_writes(this);

	/* this is the case of multiple fstrims without any changes */
	if (cpc->reason & CP_DISCARD)
	{
		if (!f2fs_exist_trim_candidates(this, cpc)) 
		{
			unblock_operations(this);
			goto out;
		}

		if (NM_I(this)->nat_cnt[DIRTY_NAT] == 0 && SIT_I()->dirty_sentries == 0 &&	prefree_segments() == 0) 
		{
			f2fs_flush_sit_entries(cpc);
			f2fs_clear_prefree_segments(this, cpc);
			unblock_operations(this);
			goto out;
		}
	}

	/* update checkpoint pack index Increase the version number so that SIT entries and seg summaries are written at correct place	 */
	ckpt_ver = cur_cp_version(ckpt);
	ckpt->checkpoint_ver = cpu_to_le64(++ckpt_ver);
	LOG_DEBUG(L"write checkpoint, version=%d", ckpt_ver);

	/* write cached NAT/SIT entries to NAT/SIT area */
	err = f2fs_flush_nat_entries(this, cpc);
	if (err)		goto stop;

	f2fs_flush_sit_entries(cpc);

	/* save inmem log status */
	f2fs_save_inmem_curseg(this);

	err = do_checkpoint(cpc);
	if (err)		f2fs_release_discard_addrs(this);
	else		f2fs_clear_prefree_segments(this, cpc);

	f2fs_restore_inmem_curseg(this);
stop:
	unblock_operations(this);
	stat_inc_cp_count(stat_info);

	if (cpc->reason & CP_RECOVERY) LOG_WARNING(L"checkpoint: version = %llx", ckpt_ver);

	/* update CP_TIME to trigger checkpoint periodically */
	this->f2fs_update_time(CP_TIME);
	//trace_f2fs_write_checkpoint(sb, cpc->reason, "finish checkpoint");
out:
	if (cpc->reason != CP_RESIZE) up_write(&this->cp_global_sem);
	return err;
}

void f2fs_init_ino_entry_info(f2fs_sb_info *sbi)
{
	int i;

	for (i = 0; i < MAX_INO_ENTRY; i++) 
	{
		struct inode_management *im = &sbi->im[i];
		//<YUAN>暂时用std::map代替radix_tree_root，不需要初始化
//		INIT_RADIX_TREE(&im->ino_root, GFP_ATOMIC);
		spin_lock_init(&im->ino_lock);
		INIT_LIST_HEAD(&im->ino_list);
		im->ino_num = 0;
	}
	sbi->max_orphans = (sbi->blocks_per_seg - F2FS_CP_PACKS - NR_CURSEG_PERSIST_TYPE - sbi->__cp_payload()) 
		* F2FS_ORPHANS_PER_BLOCK;
}
#if 0

int __init f2fs_create_checkpoint_caches(void)
{
	ino_entry_slab = f2fs_kmem_cache_create("f2fs_ino_entry",
			sizeof(struct ino_entry));
	if (!ino_entry_slab)
		return -ENOMEM;
	f2fs_inode_entry_slab = f2fs_kmem_cache_create("f2fs_inode_entry",
			sizeof(struct inode_entry));
	if (!f2fs_inode_entry_slab) {
		kmem_cache_destroy(ino_entry_slab);
		return -ENOMEM;
	}
	return 0;
}

void f2fs_destroy_checkpoint_caches(void)
{
	kmem_cache_destroy(ino_entry_slab);
	kmem_cache_destroy(f2fs_inode_entry_slab);
}
#endif

int ckpt_req_control::__write_checkpoint_sync()
{
	cp_control cpc;
	cpc.reason = CP_SYNC;
	int err=0;

	down_write(&m_sbi->gc_lock);
	err = m_sbi->f2fs_write_checkpoint(&cpc);
	up_write(&m_sbi->gc_lock);
	return err;
}

//static void flush_remained_ckpt_reqs(f2fs_sb_info *sbi, ckpt_req *wait_req)
void ckpt_req_control::flush_remained_ckpt_reqs(ckpt_req* wait_req)
{
//	struct ckpt_req_control *cprc = &sbi->cprc_info;

	if (!llist_empty(&issue_list)) 
	{
		__checkpoint_and_complete_reqs();
	} 
	else 
	{	/* already dispatched by issue_checkpoint_thread */
//		if (wait_req) 		wait_for_completion(&wait_req->wait);
		if (wait_req) 		wait_req->WaitForComplete();
	}
}

//<YUAN> move to ckpt_req member
//static void init_ckpt_req(struct ckpt_req *req)
//{
//	memset(req, 0, sizeof(struct ckpt_req));
//	init_completion(&req->wait);
//	req->queue_time = ktime_get();
//}

int f2fs_issue_checkpoint(f2fs_sb_info* sbi)
{
	ckpt_req_control *cprc = &sbi->cprc_info;
	cp_control cpc;

	cpc.reason = sbi->__get_cp_reason();
	if (!test_opt(sbi, MERGE_CHECKPOINT) || cpc.reason != CP_SYNC) {
		int ret;

		down_write(&sbi->gc_lock);
		ret = sbi->f2fs_write_checkpoint( &cpc);
		up_write(&sbi->gc_lock);

		return ret;
	}
#ifdef CONFIG_SYNC_CHECKPT
//	if (!cprc->f2fs_issue_ckpt)
	if (!cprc->IsRunning())		return cprc->__write_checkpoint_sync();
#endif
	ckpt_req req(false);
	//init_ckpt_req(&req);	// 构造函数实现
	cprc->AddRequest(&req);

	//llist_add(&req.llnode, &cprc->issue_list);
	//atomic_inc(&cprc->queued_ckpt);

	/* update issue_list before we wake up issue_checkpoint thread, this smp_mb() pairs with another barrier in
	   ___wait_event(), see more details in comments of waitqueue_active().	 */
	//smp_mb();

	//if (waitqueue_active(&cprc->ckpt_wait_queue))
	//	wake_up(&cprc->ckpt_wait_queue);

//	if (cprc->f2fs_issue_ckpt) wait_for_completion(&req.wait);
	if (cprc->IsRunning()) 		req.WaitForComplete();
	else						cprc->flush_remained_ckpt_reqs(&req);
	return req.ret;
}

//int f2fs_sb_info::f2fs_start_ckpt_thread()
//{
////	dev_t dev = sb->s_bdev->bd_dev;
//	ckpt_req_control &cprc = cprc_info;
//
//	if (cprc.f2fs_issue_ckpt)		return 0;
//	cprc.f2fs_issue_ckpt = CreateThread(NULL, 0, issue_checkpoint_thread, this, 0, &cprc.thread_id);
//	//	cprc.f2fs_issue_ckpt = kthread_run(issue_checkpoint_thread, sbi, "f2fs_ckpt-%u:%u", MAJOR(dev), MINOR(dev));
//	//if (IS_ERR(cprc->f2fs_issue_ckpt)) 
//	if (cprc.f2fs_issue_ckpt == NULL)
//	{
//		LOG_WIN32_ERROR(L"[err] failed on starting checkpoint thread");
////		cprc->f2fs_issue_ckpt = NULL;
//		return -ENOMEM;
//	}
//	//<YUAN> ckpt_thread_ioprio的缺省级别是 DEFAULT_CHECKPOINT_IOPRIO=(IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 3))
//	//	从IOPRIO_CLASS_BE减3级，IOPRIO_CLASS_BE是中间级别
//	//set_task_ioprio(cprc->f2fs_issue_ckpt, cprc->ckpt_thread_ioprio);
//	SetThreadPriority(cprc.f2fs_issue_ckpt, THREAD_PRIORITY_BELOW_NORMAL);
//	return 0;
//}
#if 0

void f2fs_stop_ckpt_thread(struct f2fs_sb_info *sbi)
{
	struct ckpt_req_control *cprc = &sbi->cprc_info;

	if (cprc->f2fs_issue_ckpt) {
		struct task_struct *ckpt_task = cprc->f2fs_issue_ckpt;

		cprc->f2fs_issue_ckpt = NULL;
		kthread_stop(ckpt_task);

		flush_remained_ckpt_reqs(sbi, NULL);
	}
}
#endif

//void f2fs_init_ckpt_req_control(f2fs_sb_info *sbi)
//{
//	struct ckpt_req_control *cprc = &sbi->cprc_info;
//
//	atomic_set(&cprc->issued_ckpt, 0);
//	atomic_set(&cprc->total_ckpt, 0);
//	atomic_set(&cprc->queued_ckpt, 0);
//	cprc->ckpt_thread_ioprio = DEFAULT_CHECKPOINT_IOPRIO;
//#if 0	// 检查ckpt_wait_queue的目的
//	init_waitqueue_head(&cprc->ckpt_wait_queue);
//	init_llist_head(&cprc->issue_list);
//#endif
//	spin_lock_init(&cprc->stat_lock);
//}



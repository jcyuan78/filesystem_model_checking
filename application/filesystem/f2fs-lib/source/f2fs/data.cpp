﻿///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"
// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/data.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include "../../include/f2fs_fs.h"
#include "../../include/f2fs-filesystem.h"
#include "../../include/io-complete-ctrl.h"
//
#include "../../include/f2fs.h"
#include "node.h"
#include "segment.h"
//#include <trace/events/f2fs.h>

#include "../mapping.h"

#include <boost/cast.hpp>

#define NUM_PREALLOC_POST_READ_CTXS	128

//static struct kmem_cache *bio_post_read_ctx_cache;
//static struct kmem_cache *bio_entry_slab;
//static mempool_t *bio_post_read_ctx_pool;
//static struct bio_set f2fs_bioset;

#define	F2FS_BIO_POOL_SIZE	NR_CURSEG_TYPE

LOCAL_LOGGER_ENABLE(L"f2fs.data", LOGGER_LEVEL_NOTICE);

#if 0

int __init f2fs_init_bioset(void)
{
	if (bioset_init(&f2fs_bioset, F2FS_BIO_POOL_SIZE,
					0, BIOSET_NEED_BVECS))
		return -ENOMEM;
	return 0;
}

void f2fs_destroy_bioset(void)
{
	bioset_exit(&f2fs_bioset);
}
#endif
static bool __is_cp_guaranteed(page *page)
{
	address_space *mapping = page->mapping;
//	struct inode *inode;
	struct f2fs_sb_info *sbi;

	if (!mapping)		return false;
	if (f2fs_is_compressed_page(page))		return false;

	inode *iinode = mapping->host;
	sbi = F2FS_I_SB(iinode);

	if (iinode->i_ino == sbi->F2FS_META_INO() || iinode->i_ino == sbi->F2FS_NODE_INO() ||	S_ISDIR(iinode->i_mode) || 
		(S_ISREG(iinode->i_mode) &&	(f2fs_is_atomic_file(F2FS_I(iinode)) || IS_NOQUOTA(iinode))) || is_cold_data(page))
		return true;
	return false;
}

static count_type __read_io_type(page *page)
{
	address_space* mapping = page->mapping;// page_file_mapping(page);

	if (mapping) 
	{
		inode *inode = mapping->host;
		f2fs_sb_info *sbi = F2FS_I_SB(inode);
		if (inode->i_ino == sbi->F2FS_META_INO())			return F2FS_RD_META;
		if (inode->i_ino == sbi->F2FS_NODE_INO())			return F2FS_RD_NODE;
	}
	return F2FS_RD_DATA;
}

/* postprocessing steps for read bios */
enum bio_post_read_step {
#ifdef CONFIG_FS_ENCRYPTION
	STEP_DECRYPT	= 1 << 0,
#else
	STEP_DECRYPT	= 0,	/* compile out the decryption-related code */
#endif
#ifdef CONFIG_F2FS_FS_COMPRESSION
	STEP_DECOMPRESS	= 1 << 1,
#else
	STEP_DECOMPRESS	= 0,	/* compile out the decompression-related code */
#endif
#ifdef CONFIG_FS_VERITY
	STEP_VERITY	= 1 << 2,
#else
	STEP_VERITY	= 0,	/* compile out the verity-related code */
#endif
};

struct work_struct
{
};

struct bio_post_read_ctx 
{
	struct bio *bio;
	struct f2fs_sb_info *sbi;
	struct work_struct work;
	unsigned int enabled_steps;
};

static void f2fs_finish_read_bio(struct bio *bio)
{
	bvec_iter_all iter_all;

	/* Update and unlock the bio's pagecache pages, and put the decompression context for any compressed pages. */
//	bio_for_each_segment_all(bv, bio, iter_all) 
	for (bio_vec * bv = bvec_init_iter_all(&iter_all); bio_next_segment((bio), &iter_all); )
	{
		page *page = bv->bv_page;
		if (f2fs_is_compressed_page(page))
		{
			if (bio->bi_status)		f2fs_end_read_compressed_page(page, true);
			f2fs_put_page_dic(page);
			continue;
		}

		/* PG_error was set if decryption or verity failed. */
		if (bio->bi_status || PageError(page)) 
		{
			ClearPageUptodate(page);
			ClearPageError(page);/* will re-read again later */
		} 
		else 
		{
			SetPageUptodate(page);
			page->WakeUpPageBit(PG_uptodate);
		}
		F2FS_P_SB(page)->dec_page_count(__read_io_type(page));
		unlock_page(page);
	}

	LOG_TRACK(L"bio", L"bio=%p, call bio_put", bio);
	delete (bio->bi_private);
	bio_put(bio);
}

#if 0

static void f2fs_verify_bio(struct work_struct *work)
{
	struct bio_post_read_ctx *ctx =
		container_of(work, struct bio_post_read_ctx, work);
	struct bio *bio = ctx->bio;
	bool may_have_compressed_pages = (ctx->enabled_steps & STEP_DECOMPRESS);

	/* fsverity_verify_bio() may call readpages() again, and while verity will be disabled for this, decryption and/or decompression may still be needed, resulting in another bio_post_read_ctx being allocated. So to prevent deadlocks we need to release the current ctx to the mempool first.  This assumes that verity is the last post-read step. */
	mempool_free(ctx, bio_post_read_ctx_pool);
	bio->bi_private = NULL;

	/* Verify the bio's pages with fs-verity.  Exclude compressed pages, as those were handled separately by f2fs_end_read_compressed_page(). */
	if (may_have_compressed_pages) {
		struct bio_vec *bv;
		struct bvec_iter_all iter_all;

		bio_for_each_segment_all(bv, bio, iter_all) {
			struct page *page = bv->bv_page;

			if (!f2fs_is_compressed_page(page) &&
			    !PageError(page) && !fsverity_verify_page(page))
				SetPageError(page);
		}
	} else {
		fsverity_verify_bio(bio);
	}

	f2fs_finish_read_bio(bio);
}
#endif

/* If the bio's data needs to be verified with fs-verity, then enqueue the verity work for the bio.  Otherwise finish the bio now.
 * Note that to avoid deadlocks, the verity work can't be done on the decryption/decompression workqueue.  This is because verifying the data pages can involve reading verity metadata pages from the file, and these verity metadata pages may be encrypted and/or compressed. */
static void f2fs_verify_and_finish_bio(bio *bio)
{
	bio_post_read_ctx *ctx = (bio_post_read_ctx*) bio->bi_private;

	if (ctx && (ctx->enabled_steps & STEP_VERITY))
	{
#if 0	//<TODO>
		INIT_WORK(&ctx->work, f2fs_verify_bio);
		fsverity_enqueue_verify_work(&ctx->work);
#endif
		delete ctx;
	} 
	else
	{
		f2fs_finish_read_bio(bio);
	}
}
#if 0
/*
 * Handle STEP_DECOMPRESS by decompressing any compressed clusters whose last
 * remaining page was read by @ctx->bio.
 *
 * Note that a bio may span clusters (even a mix of compressed and uncompressed
 * clusters) or be for just part of a cluster.  STEP_DECOMPRESS just indicates
 * that the bio includes at least one compressed page.  The actual decompression
 * is done on a per-cluster basis, not a per-bio basis.
 */
static void f2fs_handle_step_decompress(struct bio_post_read_ctx *ctx)
{
	struct bio_vec *bv;
	struct bvec_iter_all iter_all;
	bool all_compressed = true;

	bio_for_each_segment_all(bv, ctx->bio, iter_all) {
		struct page *page = bv->bv_page;

		/* PG_error was set if decryption failed. */
		if (f2fs_is_compressed_page(page))
			f2fs_end_read_compressed_page(page, PageError(page));
		else
			all_compressed = false;
	}

	/*
	 * Optimization: if all the bio's pages are compressed, then scheduling
	 * the per-bio verity work is unnecessary, as verity will be fully
	 * handled at the compression cluster level.
	 */
	if (all_compressed)
		ctx->enabled_steps &= ~STEP_VERITY;
}

static void f2fs_post_read_work(struct work_struct *work)
{
	struct bio_post_read_ctx *ctx =
		container_of(work, struct bio_post_read_ctx, work);

	if (ctx->enabled_steps & STEP_DECRYPT)
		fscrypt_decrypt_bio(ctx->bio);

	if (ctx->enabled_steps & STEP_DECOMPRESS)
		f2fs_handle_step_decompress(ctx);

	f2fs_verify_and_finish_bio(ctx->bio);
}
#endif
static void f2fs_read_end_io(bio *bio)
{
//	struct f2fs_sb_info *sbi = F2FS_P_SB(bio_first_page_all(bio));
	f2fs_sb_info *sbi = F2FS_P_SB(bio->bi_io_vec->bv_page);
	bio_post_read_ctx *ctx = (bio_post_read_ctx*)bio->bi_private;

	if (time_to_inject(sbi, FAULT_READ_IO))
	{
		f2fs_show_injection_info(sbi, FAULT_READ_IO);
		bio->bi_status = BLK_STS_IOERR;
	}

	if (bio->bi_status)
	{
		LOG_ERROR(L"[err] bio error, status=%d", (int)(bio->bi_status));
		f2fs_finish_read_bio(bio);
		return;
	}

	if (ctx && (ctx->enabled_steps & (STEP_DECRYPT | STEP_DECOMPRESS))) 
	{
#if 0	//<TODO>
		INIT_WORK(&ctx->work, f2fs_post_read_work);
		queue_work(ctx->sbi->post_read_wq, &ctx->work);
#endif
		delete ctx;
	}
	else 
	{
		f2fs_verify_and_finish_bio(bio);
	}
}

void CF2fsFileSystem::f2fs_write_end_io(bio* bb)
{
	f2fs_sb_info *sbi = (f2fs_sb_info*)(bb->bi_private);
	sbi->m_fs->write_end_io(bb);
}

void CF2fsFileSystem::write_end_io(bio* bb)
{
	bio_vec *bvec;
	bvec_iter_all iter_all;

	if (time_to_inject(m_sb_info, FAULT_WRITE_IO)) 
	{
		f2fs_show_injection_info(m_sb_info, FAULT_WRITE_IO);
		bb->bi_status = BLK_STS_IOERR;
	}

	bio_for_each_segment_all(bvec, bb, iter_all) 
	{
		page *ppage = bvec->bv_page;
		enum count_type type = WB_DATA_TYPE(ppage);

		if (IS_DUMMY_WRITTEN_PAGE(ppage)) 
		{
			set_page_private(ppage, (unsigned long)NULL);
			ClearPagePrivate(ppage);
			unlock_page(ppage);
//			mempool_free(ppage, m_sb_info->write_io_dummy);
			if (unlikely(bb->bi_status))
			{
				LOG_ERROR(L"[err] failed on bio status for dummy, stop checkpoint");
				f2fs_stop_checkpoint(m_sb_info, true);
			}
			continue;
		}

		fscrypt_finalize_bounce_page(&ppage);

#ifdef CONFIG_F2FS_FS_COMPRESSION
		if (f2fs_is_compressed_page(ppage)) 
		{
			f2fs_compress_write_end_io(bb, ppage);		continue;
		}
#endif

		if (unlikely(bb->bi_status)) 
		{
			mapping_set_error(ppage->mapping, -EIO);
			LOG_ERROR(L"[err] failed on bio status, stop checkpoint");
			if (type == F2FS_WB_CP_DATA)		f2fs_stop_checkpoint(m_sb_info, true);
		}

		f2fs_bug_on(m_sb_info, ppage->mapping == NODE_MAPPING(m_sb_info) &&	ppage->index != nid_of_node(ppage));

		m_sb_info->dec_page_count(type);
		if (m_sb_info->f2fs_in_warm_node_list( ppage))	f2fs_del_fsync_node_entry(m_sb_info, ppage);
		clear_cold_data(ppage);
		end_page_writeback(ppage);
	}
	if (!m_sb_info->get_pages(F2FS_WB_CP_DATA)/* && wq_has_sleeper(m_sb_info->cp_wait)*/)
	{
//		wake_up(m_sb_info->cp_wait);
		m_sb_info->cprc_info.WakeUp();
	}
	LOG_TRACK(L"bio", L"bio=%p, call bio_put", bb);
	bio_put(bb);
}

//IVirtualDisk *f2fs_target_device(f2fs_sb_info *sbi, block_t blk_addr, bio *bio)
// 这个函数的可读性很差。它做了两件关联性很小的事情：（1）获取blk_addr对应的dev，并且返回。（2）设置bio的dev，并且将blkaddr转换成sector。
IVirtualDisk* f2fs_sb_info::f2fs_target_device(block_t blk_addr, bio* bio)
{
	IVirtualDisk *bdev = s_bdev;
	int i;

	if (f2fs_is_multi_device()) 
	{
		for (i = 0; i < s_ndevs; i++)
		{
			if (devs[i].start_blk <= blk_addr && devs[i].end_blk >= blk_addr)
			{
				blk_addr -= devs[i].start_blk;
				bdev = devs[i].m_disk;
				break;
			}
		}
	}
	if (bio) 
	{
		bio_set_dev(bio, bdev);
		bio->bi_iter.bi_sector = SECTOR_FROM_BLOCK(blk_addr);
	}
	return bdev;
}

int f2fs_target_device_index(struct f2fs_sb_info *sbi, block_t blkaddr)
{
	int i;

	if (!sbi->f2fs_is_multi_device())
		return 0;

	for (i = 0; i < sbi->s_ndevs; i++)
		if (FDEV(i).start_blk <= blkaddr && FDEV(i).end_blk >= blkaddr)
			return i;
	return 0;
}

//bio *CF2fsFileSystem::__bio_alloc(f2fs_io_info *fio, int npages)
bio *f2fs_sb_info::__bio_alloc(f2fs_io_info *fio, int npages)
{
	CF2fsFileSystem* fs = m_fs;
//	f2fs_sb_info *sbi = fio->sbi;
	bio *ptr_bio;

	//ptr_bio = bio_alloc_bioset(GFP_NOIO, npages, &f2fs_bioset);
	ptr_bio = m_io_control->bio_alloc_bioset(GFP_NOIO, npages);

	f2fs_target_device(fio->new_blkaddr, ptr_bio);
	if (is_read_io(fio->op)) 
	{
		ptr_bio->bi_end_io = f2fs_read_end_io;
		ptr_bio->bi_private = nullptr;
	}
	else 
	{
		ptr_bio->bi_end_io = m_fs->f2fs_write_end_io;
		ptr_bio->bi_private = this;
		ptr_bio->bi_write_hint = f2fs_io_type_to_rw_hint(this, fio->type, fio->temp);
	}
#if 0
	if (fio->io_wbc)		wbc_init_bio(fio->io_wbc, ptr_bio);
#endif
	return ptr_bio;
}


static void f2fs_set_bio_crypt_ctx(bio *bio, const inode *inode, pgoff_t first_idx, const f2fs_io_info *fio,  gfp_t gfp_mask)
{
	/* The f2fs garbage collector sets ->encrypted_page when it wants to read/write raw data without encryption. */
	if (!fio || !fio->encrypted_page)
	{
//		JCASSERT(0);
#if 0	//TODO
		fscrypt_set_bio_crypt_ctx(bio, inode, first_idx, gfp_mask);
#endif
	}
}
static bool f2fs_crypt_mergeable_bio(bio *bio, const inode *node, pgoff_t next_idx, const f2fs_io_info *fio)
{
	/* The f2fs garbage collector sets ->encrypted_page when it wants to read/write raw data without encryption. */
	if (fio && fio->encrypted_page) return false;
#if 0 //TODO
		return !bio_has_crypt_ctx(bio);
#endif

	return fscrypt_mergeable_bio(bio, node, next_idx);
}

void f2fs_sb_info::__submit_bio(bio *bio, enum page_type type)
{
	if (!is_read_io(bio_op(bio))) 
	{
		size_t start;
		if (type != DATA && type != NODE)		goto submit_io;
#if 0
		// blk_finish_plug 似乎什么都没做。
		if (f2fs_lfs_mode(sbi) && current->plug)		blk_finish_plug(current->plug);
#endif
		size_t io_size = F2FS_IO_SIZE(this);
		if (!F2FS_IO_ALIGNED(this))				goto submit_io;
		start = bio->bi_iter.bi_size >> F2FS_BLKSIZE_BITS;
		start %= io_size;
		if (start == 0)							goto submit_io;

		/* fill dummy pages */ 	// 仅对不对齐的情况进行处理
		for (; start < io_size; start++) 
		{
			page* pp = GetPageManager()->NewPage();
			f2fs_bug_on(this, !pp);
			zero_user_segment(pp, 0, PAGE_SIZE);
			SetPagePrivate(pp);
			set_page_private(pp, DUMMY_WRITTEN_PAGE);
			lock_page(pp);
			LOG_DEBUG_(1,L"add dummy page to bio, page=%llX, flag=%X", pp, pp->flags);
			if (bio_add_page(bio, pp, PAGE_SIZE, 0) < PAGE_SIZE)	f2fs_bug_on(this, 1);
		}
		/* In the NODE case, we lose next block address chain. So, we need to do checkpoint in f2fs_sync_file. */
		if (type == NODE)		set_sbi_flag(SBI_NEED_CP);
	}
submit_io:
	//if (is_read_io(bio_op(bio)))		trace_f2fs_submit_read_bio(sbi->sb, type, bio);
	//else								trace_f2fs_submit_write_bio(sbi->sb, type, bio);
//	submit_bio(bio);
	if (bio->bi_end_io)		m_io_control->submit_async_io(bio);
	else					submit_sync_io(bio);
}

//void f2fs_submit_bio(struct f2fs_sb_info *sbi, struct bio *bio, enum page_type type)
//{
//	sbi->__submit_bio( bio, type);
//}

static void __attach_io_flag(struct f2fs_io_info *fio)
{
	struct f2fs_sb_info *sbi = fio->sbi;
	unsigned int temp_mask = (1 << NR_TEMP_TYPE) - 1;
	unsigned int io_flag, fua_flag, meta_flag;

	if (fio->type == DATA)			io_flag = sbi->data_io_flag;
	else if (fio->type == NODE)		io_flag = sbi->node_io_flag;
	else		return;

	fua_flag = io_flag & temp_mask;
	meta_flag = (io_flag >> NR_TEMP_TYPE) & temp_mask;

	/* data/node io flag bits per temp:
	 *      REQ_META     |      REQ_FUA      |
	 *    5 |    4 |   3 |    2 |    1 |   0 |
	 * Cold | Warm | Hot | Cold | Warm | Hot |	 */
	if ((1 << fio->temp) & meta_flag)		fio->op_flags |= REQ_META;
	if ((1 << fio->temp) & fua_flag)		fio->op_flags |= REQ_FUA;
}


//static void __submit_merged_bio(f2fs_bio_info *io)
void f2fs_sb_info::__submit_merged_bio(f2fs_bio_info* io)
{
	f2fs_io_info *fio = &io->fio;
	if (!io->bio)	return;
	LOG_DEBUG_(1,L"submit bio, sector=0x%llX, size=0x%llX", io->bio->bi_iter.bi_sector, io->bio->bi_iter.bi_size);

	__attach_io_flag(fio);
	bio_set_op_attrs(io->bio, fio->op, fio->op_flags);

#if 0	//<TRACE>
	if (is_read_io(fio->op))		trace_f2fs_prepare_read_bio(io->sbi->sb, fio->type, io->bio);
	else		trace_f2fs_prepare_write_bio(io->sbi->sb, fio->type, io->bio);
#endif
	__submit_bio(io->bio, fio->type);
	io->bio = NULL;
}

static bool __has_merged_page(bio *bio, inode *iinode, page *ppage, nid_t ino)
{
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	if (!bio)	return false;

	if (!iinode && !ppage && !ino)	return true;

	bio_for_each_segment_all(bvec, bio, iter_all) 
	{
		page *target = bvec->bv_page;

#if 0
		if (fscrypt_is_bounce_page(target)) 
		{
			target = fscrypt_pagecache_page(target);
			if (IS_ERR(target))			continue;
		}
#endif
		if (f2fs_is_compressed_page(target)) 
		{
			target = f2fs_compress_control_page(target);
			if (IS_ERR(target))			continue;
		}
		// return true 表示不能merge
		if (iinode && iinode == target->mapping->host)	return true;
		if (ppage && ppage == target) 					return true;
		if (ino && ino == ino_of_node(target))			return true;
	}
	return false;
}

//static void __f2fs_submit_merged_write(f2fs_sb_info *sbi, enum page_type type, enum temp_type temp)
void f2fs_sb_info::__f2fs_submit_merged_write(enum page_type type, enum temp_type temp)
{
	enum page_type btype = PAGE_TYPE_OF_BIO(type);
	struct f2fs_bio_info *io = write_io[btype] + temp;

	down_write(&io->io_rwsem);

	/* change META to META_FLUSH in the checkpoint procedure */
	if (type >= META_FLUSH) 
	{
		io->fio.type = META_FLUSH;
		io->fio.op = REQ_OP_WRITE;
		io->fio.op_flags = REQ_META | REQ_PRIO | REQ_SYNC;
		if (!test_opt_(F2FS_MOUNT_NOBARRIER))		io->fio.op_flags |= REQ_PREFLUSH | REQ_FUA;
	}
	__submit_merged_bio(io);
	up_write(&io->io_rwsem);
}

static void __submit_merged_write_cond(f2fs_sb_info *sbi, inode *iinode, page *ppage, nid_t ino, enum page_type type, bool force)
{
//	LOG_TRACK(L"io",L"ino=%d, page=%llX, page_index=%d", ino, ppage, ppage?(ppage->index):-1);
	bool ret = true;

	for (int tt = HOT; tt < NR_TEMP_TYPE; tt++) 
	{
		temp_type temp = (temp_type)tt;
		if (!force)	
		{
			enum page_type btype = PAGE_TYPE_OF_BIO(type);
			f2fs_bio_info *io = sbi->write_io[btype] + temp;

			down_read(&io->io_rwsem);
			ret = __has_merged_page(io->bio, iinode, ppage, ino);
			up_read(&io->io_rwsem);
		}
		if (ret) sbi->__f2fs_submit_merged_write(type, temp);

		/* TODO: use HOT temp only for meta pages now. */
		if (type >= META)	break;
	}
}

void f2fs_submit_merged_write(struct f2fs_sb_info *sbi, enum page_type type)
{
	__submit_merged_write_cond(sbi, NULL, NULL, 0, type, true);
}

void f2fs_submit_merged_write_cond(struct f2fs_sb_info *sbi, struct inode *inode, struct page *page,
				nid_t ino, enum page_type type)
{
	__submit_merged_write_cond(sbi, inode, page, ino, type, false);
}

void f2fs_flush_merged_writes(struct f2fs_sb_info *sbi)
{
	f2fs_submit_merged_write(sbi, DATA);
	f2fs_submit_merged_write(sbi, NODE);
	f2fs_submit_merged_write(sbi, META);
}

//static int waiton_page_uptodate(page* ppage)
//{
//	while (!PageUptodate(ppage))
//	{
//		SleepEx(0, TRUE);
//		wait_on_page_bit_killable_positive(ppage, PG_uptodate);
//	}
//	return 1;
//}

/* Fill the locked page with data located in the block address. A caller needs to unlock the page on failure. */
int f2fs_sb_info::f2fs_submit_page_bio(f2fs_io_info *fio)
{
	//每次读1个page, 这里似乎要求sync read
//	LOG_TRACK(L"io", L"submit bio: op=%s, addr=0x%X", DebugOutReq(fio->op), fio->new_blkaddr);

	struct bio *bio;
	page *ppage = fio->encrypted_page ? fio->encrypted_page : fio->page;

	if (!f2fs_is_valid_blkaddr(fio->new_blkaddr, 
		fio->is_por ? META_POR : (__is_meta_io(fio) ? META_GENERIC : DATA_GENERIC_ENHANCE)))
	{
		LOG_ERROR(L"blkaddr is not valid, add=%04X", fio->new_blkaddr);
		return -EFSCORRUPTED;
	}
//	trace_f2fs_submit_page_bio(page, fio);
	/* Allocate a new bio */
	bio = __bio_alloc(fio, 1);
	f2fs_set_bio_crypt_ctx(bio, fio->page->mapping->host, fio->page->index, fio, GFP_NOIO);
	//<YUAN> 设置bio的 bv_page, bv_offset, bv_len, 等
	// offset为bio_vec数据在page中的偏移量
	TRACK_BIO_IO(bio, L"add page: ino=%d, index=%d", fio->ino, ppage->index);
	if (bio_add_page(bio, ppage, PAGE_SIZE, 0) < PAGE_SIZE) 
	{
		bio_put(bio);
		return -EFAULT;
	}

	if (fio->io_wbc && !is_read_io(fio->op))	wbc_account_cgroup_owner(fio->io_wbc, ppage, PAGE_SIZE);
	__attach_io_flag(fio);
	bio_set_op_attrs(bio, fio->op, fio->op_flags);

	fio->sbi->inc_page_count( is_read_io(fio->op) ?	__read_io_type(ppage): WB_DATA_TYPE(fio->page));

	__submit_bio(bio, fio->type);
//	waiton_page_uptodate(ppage);

	return 0;
}


//static bool page_is_mergeable(f2fs_sb_info *sbi, bio *bio, block_t last_blkaddr, block_t cur_blkaddr)
bool f2fs_sb_info::page_is_mergeable(bio* bio, block_t last_blkaddr, block_t cur_blkaddr)
{
	LOG_DEBUG_(1,L"max_io_bytes=%d, bio::bi_size=%d", max_io_bytes, bio->bi_iter.bi_size);
	if (unlikely(this->max_io_bytes && bio->bi_iter.bi_size >= this->max_io_bytes))	return false;
	if (last_blkaddr + 1 != cur_blkaddr)	return false;
	return bio->bi_bdev == f2fs_target_device(cur_blkaddr, NULL);
}

static bool io_type_is_mergeable(f2fs_bio_info *io, f2fs_io_info *fio)
{
	if (io->fio.op != fio->op)	return false;
	return io->fio.op_flags == fio->op_flags;
}

//static bool io_is_mergeable(f2fs_sb_info *sbi, bio *bio, f2fs_bio_info *io, f2fs_io_info *fio,
//					block_t last_blkaddr, block_t cur_blkaddr)
bool f2fs_sb_info::io_is_mergeable(bio* bio, f2fs_bio_info* io, f2fs_io_info* fio, block_t last_blkaddr, block_t cur_blkaddr)
{
	if (F2FS_IO_ALIGNED(this) && (fio->type == DATA || fio->type == NODE))
	{
		size_t filled_blocks = F2FS_BYTES_TO_BLK(bio->bi_iter.bi_size);
		size_t io_size = F2FS_IO_SIZE(this);
		unsigned int left_vecs = bio->bi_max_vecs - bio->bi_vcnt;

		/* IOs in bio is aligned and left space of vectors is not enough */
		if (!(filled_blocks % io_size) && left_vecs < io_size)		return false;
	}
	if (!page_is_mergeable(bio, last_blkaddr, cur_blkaddr))	return false;
	return io_type_is_mergeable(io, fio);
}

static void add_bio_entry(struct f2fs_sb_info *sbi, struct bio *bio, struct page *page, enum temp_type temp)
{
	struct f2fs_bio_info *io = sbi->write_io[DATA] + temp;
	struct bio_entry *be;

	be = f2fs_kmem_cache_alloc<bio_entry>(/*bio_entry_slab*/NULL, GFP_NOFS);
	be->bio = bio;
	bio_get(bio);

	if (bio_add_page(bio, page, PAGE_SIZE, 0) != PAGE_SIZE)
	{
		JCASSERT(0);
		f2fs_bug_on(sbi, 1);
	}

	down_write(&io->bio_list_lock);
	list_add_tail(&be->list, &io->bio_list);
	up_write(&io->bio_list_lock);
}

static void del_bio_entry(struct bio_entry *be)
{
	list_del(&be->list);
	kmem_cache_free(/*bio_entry_slab*/NULL, be);
}

static int add_ipu_page(f2fs_io_info *fio, bio **bio, page *ppage)
{
	f2fs_sb_info *sbi = fio->sbi;
	enum temp_type temp;
	bool found = false;
	int ret = -EAGAIN;

	for (int tt =HOT; tt < NR_TEMP_TYPE && !found; tt++) 
	{
		temp = (temp_type)(tt);
		struct f2fs_bio_info *io = sbi->write_io[DATA] + temp;
		struct list_head *head = &io->bio_list;
		struct bio_entry *be;

		down_write(&io->bio_list_lock);
		list_for_each_entry(bio_entry, be, head, list) 
		{
			if (be->bio != *bio)			continue;

			found = true;

			f2fs_bug_on(sbi, !(sbi->page_is_mergeable(*bio,  *fio->last_block,   fio->new_blkaddr)));
			if (f2fs_crypt_mergeable_bio(*bio, fio->page->mapping->host, fio->page->index, fio) &&
			    bio_add_page(*bio, ppage, PAGE_SIZE, 0) == PAGE_SIZE)
			{
				ret = 0;
				break;
			}

			/* page can't be merged into bio; submit the bio */
			del_bio_entry(be);
			sbi->__submit_bio( *bio, DATA);
			break;
		}
		up_write(&io->bio_list_lock);
	}

	if (ret) 
	{
		LOG_TRACK(L"bio", L"bio=%p, call bio_put", *bio);
		bio_put(*bio);
		*bio = NULL;
	}

	return ret;
}

void f2fs_submit_merged_ipu_write(struct f2fs_sb_info *sbi, struct bio **bio, struct page *page)
{
	LOG_STACK_TRACE();
	//enum temp_type temp;
	bool found = false;
	struct bio *target = bio ? *bio : NULL;

	for (int tt = (int)HOT; tt < (int)NR_TEMP_TYPE && !found; tt++) 
	{
		if (bio) { LOG_TRACK(L"bio", L"bio=%p, flag=%X", *bio, (*bio)->bi_flags); }
		temp_type temp = (temp_type)tt;
		struct f2fs_bio_info *io = sbi->write_io[DATA] + temp;
		struct list_head *head = &io->bio_list;
		struct bio_entry *be;

		if (list_empty(head))		continue;

		down_read(&io->bio_list_lock);
		list_for_each_entry(bio_entry, be, head, list) 
		{
			if (target)			found = (target == be->bio);
			else				found = __has_merged_page(be->bio, NULL, page, 0);
			if (found)			break;
		}
		up_read(&io->bio_list_lock);

		if (!found)			continue;

		found = false;

		down_write(&io->bio_list_lock);
		list_for_each_entry(bio_entry, be, head, list) 
		{
			if (target)			found = (target == be->bio);
			else				found = __has_merged_page(be->bio, NULL, page, 0);
			if (found) 
			{
				target = be->bio;
				del_bio_entry(be);
				break;
			}
		}
		up_write(&io->bio_list_lock);
	}

	if (found)
	{
		sbi->__submit_bio(target, DATA);
		// 由于在submit_bio中会调用f2fs_write_end_io()，在其中会删除bio，不能再删除。
		if (bio && *bio) *bio = nullptr;
	}
	if (bio && *bio) 
	{
		LOG_TRACK(L"bio", L"bio=%p, call bio_put", *bio);
		bio_put(*bio);
		*bio = NULL;
	}
}

int f2fs_merge_page_bio(f2fs_io_info *fio)
{
	struct bio *bio = *fio->bio;
	struct page *page = fio->encrypted_page ? fio->encrypted_page : fio->page;
	f2fs_sb_info* sbi = fio->sbi;

	if (!sbi->f2fs_is_valid_blkaddr(fio->new_blkaddr, __is_meta_io(fio) ? META_GENERIC : DATA_GENERIC))
		return -EFSCORRUPTED;

	//trace_f2fs_submit_page_bio(page, fio);

	if (bio && !sbi->page_is_mergeable(bio, *fio->last_block, fio->new_blkaddr))
		f2fs_submit_merged_ipu_write(fio->sbi, &bio, NULL);
alloc_new:
	if (!bio) 
	{
		bio = sbi->__bio_alloc(fio, BIO_MAX_VECS);
		__attach_io_flag(fio);
		f2fs_set_bio_crypt_ctx(bio, fio->page->mapping->host, fio->page->index, fio, GFP_NOIO);
		bio_set_op_attrs(bio, fio->op, fio->op_flags);
		add_bio_entry(fio->sbi, bio, page, fio->temp);
	}
	else
	{
		if (add_ipu_page(fio, &bio, page)) goto alloc_new;
	}

	if (fio->io_wbc) wbc_account_cgroup_owner(fio->io_wbc, page, PAGE_SIZE);

	fio->sbi->inc_page_count( WB_DATA_TYPE(page));

	*fio->last_block = fio->new_blkaddr;
	*fio->bio = bio;

	return 0;
}

void f2fs_sb_info::f2fs_submit_page_write(f2fs_io_info* fio)
{
	//	f2fs_sb_info *sbi = fio->sbi;
		// 将fio添加到IO队列中。添加的位置为: page_type+温度。
	enum page_type btype = PAGE_TYPE_OF_BIO(fio->type);
	// io是bio的一个封装。bio包含了一组连续读/写的block
	f2fs_bio_info* io = write_io[btype] + fio->temp;
	page* bio_page;

	f2fs_bug_on(this, is_read_io(fio->op));

	down_write(&io->io_rwsem);
	while (1)
	{
		//next:
		if (fio->in_list)
		{	auto_lock<spin_locker> io_locker(io->io_lock);
//			spin_lock(&io->io_lock);
			if (::list_empty(&io->io_list))
			{
//				spin_unlock(&io->io_lock);
				break;
			}
			fio = list_first_entry(&io->io_list, f2fs_io_info, list);
			list_del(&fio->list);
//			spin_unlock(&io->io_lock);
		}

		verify_fio_blkaddr(fio);

		if (fio->encrypted_page)		bio_page = fio->encrypted_page;
		else if (fio->compressed_page)	bio_page = fio->compressed_page;
		else							bio_page = fio->page;

		/* set submitted = true as a return value */
		fio->submitted = true;

		inc_page_count(WB_DATA_TYPE(bio_page));

		if (io->bio && (!io_is_mergeable(io->bio, io, fio, io->last_block_in_bio, fio->new_blkaddr) ||
			!f2fs_crypt_mergeable_bio(io->bio, fio->page->mapping->host, bio_page->index, fio)))
		{
			//如果不能合并，则直接执行原io的读写。执行完成以后，会将原来的bio清除掉。
			__submit_merged_bio(io);
		}
		while (1)
		{
//	alloc_new:
			if (io->bio == NULL)
			{	// 申请一个新的bio
				if (F2FS_IO_ALIGNED(this) && (fio->type == DATA || fio->type == NODE) && fio->new_blkaddr & F2FS_IO_SIZE_MASK(this))
				{
					dec_page_count(WB_DATA_TYPE(bio_page));
					fio->retry = true;
					goto skip;
				}
				io->bio = __bio_alloc(fio, BIO_MAX_VECS);
				f2fs_set_bio_crypt_ctx(io->bio, fio->page->mapping->host, bio_page->index, fio, GFP_NOIO);
				io->fio = *fio;
			}
			TRACK_BIO_IO(io->bio, L"add page for write, ino=%d, index=%d", fio->ino, bio_page-> index);
			if (bio_add_page(io->bio, bio_page, PAGE_SIZE, 0) < PAGE_SIZE)
			{
				__submit_merged_bio(io);
//				goto alloc_new;
			}
			else break;
		}

#if 0 // TODO
		if (fio->io_wbc) wbc_account_cgroup_owner(fio->io_wbc, bio_page, PAGE_SIZE);
#endif

		io->last_block_in_bio = fio->new_blkaddr;

		//	trace_f2fs_submit_page_write(fio->page, fio);
	skip:
		//if (fio->in_list)	goto next;
		if (!fio->in_list) break;
	}

//out:
	if (is_sbi_flag_set( SBI_IS_SHUTDOWN) || !f2fs_is_checkpoint_ready())
		__submit_merged_bio(io);
	up_write(&io->io_rwsem);
}


bio * f2fs_sb_info::f2fs_grab_read_bio(f2fs_inode_info*inode, block_t blkaddr, unsigned nr_pages, unsigned op_flag,
				      pgoff_t first_idx, bool for_write)
{
	//f2fs_sb_info *sbi = F2FS_I_SB(inode);
	unsigned int post_read_steps = 0;

	bio * ptr_bio = m_io_control->bio_alloc_bioset(for_write ? GFP_NOIO : GFP_KERNEL, bio_max_segs(nr_pages));
	if (!ptr_bio) THROW_ERROR(ERR_MEM, L"failed on creating bio object");
	//if (!ptr_bio) return ERR_PTR(-ENOMEM);

	f2fs_set_bio_crypt_ctx(ptr_bio, inode, first_idx, NULL, GFP_NOFS);

	this->f2fs_target_device(blkaddr, ptr_bio);
	ptr_bio->bi_end_io = f2fs_read_end_io;
	bio_set_op_attrs(ptr_bio, REQ_OP_READ, op_flag);

	if (fscrypt_inode_uses_fs_layer_crypto(inode))
		post_read_steps |= STEP_DECRYPT;

	if (f2fs_need_verity(inode, first_idx))
		post_read_steps |= STEP_VERITY;

	/* STEP_DECOMPRESS is handled specially, since a compressed file might contain both compressed and uncompressed clusters.  We'll alloc_obj a bio_post_read_ctx if the file is compressed, but the caller is responsible for enabling STEP_DECOMPRESS if it's actually needed.	 */

	if (post_read_steps || f2fs_compressed_file(inode)) 
	{
		/* Due to the mempool, this never fails. */
#if 0 // TODO
		ctx = mempool_alloc(bio_post_read_ctx_pool, GFP_NOFS);
#endif
		bio_post_read_ctx * ctx = new bio_post_read_ctx;
		ctx->bio = ptr_bio;
		ctx->sbi = this;
		ctx->enabled_steps = post_read_steps;
		ptr_bio->bi_private = ctx;
	}

	return ptr_bio;
}

/* This can handle encryption stuffs */
int f2fs_sb_info::f2fs_submit_page_read(f2fs_inode_info*inode, page *page, block_t blkaddr, int op_flags, bool for_write)
{
//	f2fs_sb_info *sbi = F2FS_I_SB(inode);
	bio *bio;

	bio = this->f2fs_grab_read_bio(inode, blkaddr, 1, op_flags, page->index, for_write);
	if (IS_ERR(bio)) return PTR_ERR(bio);

	/* wait for GCed page writeback via META_MAPPING */
	f2fs_wait_on_block_writeback(inode, blkaddr);

	if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE) 
	{
		LOG_TRACK(L"bio", L"bio=%p, add page and full", bio);
		bio_put(bio);
		return -EFAULT;
	}
	ClearPageError(page);
	this->inc_page_count(F2FS_RD_DATA);
	this->f2fs_update_iostat(FS_DATA_READ_IO, F2FS_BLKSIZE);
	this->__submit_bio(bio, DATA);
	return 0;
}

static void __set_data_blkaddr(struct dnode_of_data *dn)
{
	struct f2fs_node *rn = F2FS_NODE(dn->node_page);
	__le32 *addr_array;
	int base = 0;

	if (IS_INODE(dn->node_page) && f2fs_has_extra_attr(dn->inode))
		base = dn->inode->get_extra_isize();

	/* Get physical address of data block */
	addr_array = blkaddr_in_node(rn);
	addr_array[base + dn->ofs_in_node] = cpu_to_le32(dn->data_blkaddr);
}
/*
 * Lock ordering for the change of data block address:
 * ->data_page
 *  ->node_page
 *    update block addresses in the node page
 */
void f2fs_set_data_blkaddr(struct dnode_of_data *dn)
{
	f2fs_wait_on_page_writeback(dn->node_page, NODE, true, true);
	__set_data_blkaddr(dn);
	if (set_page_dirty(dn->node_page))
		dn->node_changed = true;
}

void f2fs_update_data_blkaddr(struct dnode_of_data *dn, block_t blkaddr)
{
	dn->data_blkaddr = blkaddr;
	f2fs_set_data_blkaddr(dn);
	f2fs_update_extent_cache(dn);
}

/* dn->ofs_in_node will be returned with up-to-date last block pointer */
int f2fs_reserve_new_blocks(struct dnode_of_data *dn, blkcnt_t count)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	int err;

	if (!count)
		return 0;

	if (unlikely(is_inode_flag_set(dn->inode, FI_NO_ALLOC)))
		return -EPERM;
	if (unlikely((err = inc_valid_block_count(sbi, dn->inode, &count))))
		return err;

//	trace_f2fs_reserve_new_blocks(dn->inode, dn->nid, dn->ofs_in_node, count);

	f2fs_wait_on_page_writeback(dn->node_page, NODE, true, true);

	for (; count > 0; dn->ofs_in_node++) {
		block_t blkaddr = f2fs_data_blkaddr(dn);

		if (blkaddr == NULL_ADDR) 
		{
			dn->data_blkaddr = NEW_ADDR;
			__set_data_blkaddr(dn);
			count--;
		}
	}

	if (set_page_dirty(dn->node_page))
		dn->node_changed = true;
	return 0;
}

/* Should keep dn->ofs_in_node unchanged */
int f2fs_reserve_new_block(struct dnode_of_data *dn)
{
	unsigned int ofs_in_node = dn->ofs_in_node;
	int ret;

	ret = f2fs_reserve_new_blocks(dn, 1);
	dn->ofs_in_node = ofs_in_node;
	return ret;
}
int f2fs_reserve_block(dnode_of_data *dn, pgoff_t index)
{
	bool need_put = dn->inode_page ? false : true;
	int err;

	err = f2fs_get_dnode_of_data(dn, index, ALLOC_NODE);
	if (err)
		return err;

	if (dn->data_blkaddr == NULL_ADDR)
		err = f2fs_reserve_new_block(dn);
	if (err || need_put)
		f2fs_put_dnode(dn);
	return err;
}

int f2fs_get_block(dnode_of_data *dn, pgoff_t index)
{
	struct extent_info ei = {0, 0, 0};
	struct inode *inode = dn->inode;

	if (f2fs_lookup_extent_cache(F2FS_I(inode), index, &ei)) 
	{
		dn->data_blkaddr = ei.blk + index - ei.fofs;
		return 0;
	}

	return f2fs_reserve_block(dn, index);
}



//struct page *f2fs_get_read_data_page(inode *node, pgoff_t index, int op_flags, bool for_write)
page * f2fs_inode_info::f2fs_get_read_data_page(pgoff_t index, int op_flags, bool for_write)
{
//	address_space *mapping = node->i_mapping;
	f2fs_sb_info* sbi = F2FS_I_SB(this);
	struct dnode_of_data dn;
	struct page *page;
	struct extent_info ei = {0,0,0};
	int err;

	page = f2fs_grab_cache_page(i_mapping, index, for_write);
	if (!page) THROW_ERROR(ERR_MEM, L"failed on creating page");
//	if (!page) return ERR_PTR(-ENOMEM);

	if (f2fs_lookup_extent_cache(this, index, &ei)) 
	{
		dn.data_blkaddr = ei.blk + index - ei.fofs;
		if (!sbi->f2fs_is_valid_blkaddr(dn.data_blkaddr, DATA_GENERIC_ENHANCE_READ)) 
		{
			err = -EFSCORRUPTED;
			goto put_err;
		}
		goto got_it;
	}

	dn.set_new_dnode(this, NULL, NULL, 0);
	err = f2fs_get_dnode_of_data(&dn, index, LOOKUP_NODE);
	if (err)
		goto put_err;
	f2fs_put_dnode(&dn);

	if (unlikely(dn.data_blkaddr == NULL_ADDR)) {
		err = -ENOENT;
		goto put_err;
	}
	if (dn.data_blkaddr != NEW_ADDR && !sbi->f2fs_is_valid_blkaddr(dn.data_blkaddr, DATA_GENERIC_ENHANCE)) 
	{
		err = -EFSCORRUPTED;
		goto put_err;
	}
got_it:
	if (PageUptodate(page)) {
		unlock_page(page);
		return page;
	}

	/* A new dentry page is allocated but not able to be written, since its new node page couldn't be allocated due to -ENOSPC. In such the case, its blkaddr can be remained as NEW_ADDR. see, f2fs_add_link -> f2fs_get_new_data_page -> f2fs_init_inode_metadata. */
	if (dn.data_blkaddr == NEW_ADDR) 
	{
//		zero_user_segment(page, 0, PAGE_SIZE);	// 将page数据清零
		memset(page_address<void>(page), 0, PAGE_SIZE);
		if (!PageUptodate(page)) SetPageUptodate(page);
		unlock_page(page);
		return page;
	}

	err = sbi->f2fs_submit_page_read(this, page, dn.data_blkaddr, op_flags, for_write);
	if (err) goto put_err;
	return page;

put_err:
	f2fs_put_page(page, 1);
//	return ERR_PTR(err);
	return NULL;
}


//page *f2fs_find_data_page( inode *node, pgoff_t index)
page* f2fs_inode_info::f2fs_find_data_page(pgoff_t index)
{
//	address_space *mapping = node->i_mapping;
	page *pp;
	pp = find_get_page(i_mapping, index);
#ifdef DEBUG_PAGE
	if (pp)
	{
		pp->m_type = page::DATA_PAGE;
		pp->m_inode = i_ino;
	}
#endif
	if (pp && PageUptodate(pp)) 	return pp;
	f2fs_put_page(pp, 0);

	pp = f2fs_get_read_data_page(index, 0, false);
	if (IS_ERR(pp))		return pp;

	if (PageUptodate(pp))	return pp;

	pp->WaitOnPageUptodate();
	wait_on_page_locked(pp);

	if (unlikely(!PageUptodate(pp))) {
		f2fs_put_page(pp, 0);
		return ERR_PTR<page>(-EIO);
	}
	return pp;
}

/* If it tries to access a hole, return an error. Because, the callers, functions in dir.c and GC, should be able to know whether this page exists or not. */
//struct page *f2fs_get_lock_data_page(struct inode *inode, pgoff_t index, bool for_write)
page *f2fs_inode_info::f2fs_get_lock_data_page(pgoff_t index, bool for_write)
{
	address_space* mapping = i_mapping;
	page *ppage;
repeat:
	ppage = f2fs_get_read_data_page(index, 0, for_write);
	if (IS_ERR(ppage))
		return ppage;

	/* wait for read completion */
	ppage->WaitOnPageUptodate();
	lock_page(ppage);
	if (unlikely(ppage->mapping != mapping)) 
	{
		f2fs_put_page(ppage, 1);
		goto repeat;
	}
	if (unlikely(!PageUptodate(ppage))) 
	{
		f2fs_put_page(ppage, 1);
		return ERR_PTR<page>(-EIO);
	}
	return ppage;
}


/*
 * Caller ensures that this data page is never allocated.
 * A new zero-filled data page is allocated in the page cache.
 *
 * Also, caller should grab and release a rwsem by calling f2fs_lock_op() and f2fs_unlock_op().
 * Note that, ipage is set only by make_empty_dir, and if any error occur, ipage should be released by this function. */
//struct page *f2fs_get_new_data_page(struct inode *inode, struct page *ipage, pgoff_t index, bool new_i_size)
page* f2fs_inode_info::f2fs_get_new_data_page(page* ipage, pgoff_t index, bool new_i_size)
{
	address_space *mapping = i_mapping;
	page *ppage;
	dnode_of_data dn;
	int err;

	ppage = f2fs_grab_cache_page(mapping, index, true);
	if (!ppage)
	{
		/* before exiting, we should make sure ipage will be released if any error occur. */
		f2fs_put_page(ipage, 1);
		return ERR_PTR<page>((INT64)(-ENOMEM));
	}
#ifdef INODE_DEBUG
	jcvos::Utf8ToUnicode(ppage->m_type, "data");
	ppage->m_description = L"data of " + m_description;
	LOG_TRACK(L"page", L"page=%p, type=%s, index=%d, got page", ppage, ppage->m_type.c_str(), ppage->index);
#endif

	dn.set_new_dnode(this, ipage, NULL, 0);
	err = f2fs_reserve_block(&dn, index);
	if (err) 
	{
		f2fs_put_page(ppage, 1);
		return ERR_PTR<page>(err);
	}
	if (!ipage) f2fs_put_dnode(&dn);

	if (PageUptodate(ppage)) goto got_it;

	if (dn.data_blkaddr == NEW_ADDR) 
	{
//		zero_user_segment(ppage, 0, PAGE_SIZE);		//将page的内容置0
		memset(ppage->virtual_add, 0, PAGE_SIZE);
		if (!PageUptodate(ppage)) 		SetPageUptodate(ppage);
	} 
	else
	{
		f2fs_put_page(ppage, 1);

		/* if ipage exists, blkaddr should be NEW_ADDR */
		f2fs_bug_on(F2FS_I_SB(this), ipage);
		ppage = f2fs_get_lock_data_page(index, true);
		if (IS_ERR(ppage))	return ppage;
	}
got_it:
	if (new_i_size && i_size_read(this) < ((loff_t)(index + 1) << PAGE_SHIFT))
		f2fs_i_size_write(((loff_t)(index + 1) << PAGE_SHIFT));
	return ppage;
}


static int __allocate_data_block(dnode_of_data *dn, int seg_type)
{
	f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	f2fs_summary sum;
	node_info ni;
	block_t old_blkaddr;
	blkcnt_t count = 1;
	int err;

	if (unlikely(is_inode_flag_set(dn->inode, FI_NO_ALLOC)))		return -EPERM;

	err = NM_I(sbi)->f2fs_get_node_info( dn->nid, &ni);
	if (err)		return err;

	dn->data_blkaddr = f2fs_data_blkaddr(dn);
	if (dn->data_blkaddr != NULL_ADDR)		goto alloc;

	if (unlikely((err = inc_valid_block_count(sbi, dn->inode, &count))))		return err;

alloc:
	set_summary(&sum, dn->nid, dn->ofs_in_node, ni.version);
	old_blkaddr = dn->data_blkaddr;
	sbi->f2fs_allocate_data_block(NULL, old_blkaddr, &dn->data_blkaddr,	&sum, seg_type, NULL);
	if (GET_SEGNO(sbi, old_blkaddr) != NULL_SEGNO)
		invalidate_mapping_pages(META_MAPPING(sbi),	old_blkaddr, old_blkaddr);
	f2fs_update_data_blkaddr(dn, dn->data_blkaddr);

	/* i_size will be updated by direct_IO. Otherwise, we'll get stale data from unwritten block via dio_read. */
	return 0;
}

//int f2fs_preallocate_blocks(kiocb *iocb, iov_iter *from)
int f2fs_inode_info::f2fs_preallocate_blocks(kiocb* iocb, iov_iter* from)
{
//	struct inode *inode = file_inode(iocb->ki_filp);
//	f2fs_inode_info* inode = dynamic_cast<f2fs_inode_info*>(file_inode(iocb->ki_filp));
	JCASSERT(F2FS_I(file_inode(iocb->ki_filp)) == this);
	struct f2fs_map_blocks map;
	int flag;
	int err = 0;
	bool direct_io = iocb->ki_flags & IOCB_DIRECT;

	map.m_lblk = F2FS_BLK_ALIGN(iocb->ki_pos);
	map.m_len = F2FS_BYTES_TO_BLK(iocb->ki_pos + iov_iter_count(from));
	if (map.m_len > map.m_lblk)		map.m_len -= map.m_lblk;
	else		map.m_len = 0;

	map.m_next_pgofs = NULL;
	map.m_next_extent = NULL;
	map.m_seg_type = NO_CHECK_TYPE;
	map.m_may_create = true;

	if (direct_io) 
	{	// 不用缓存的直接读写
		map.m_seg_type = f2fs_rw_hint_to_seg_type((rw_hint)iocb->ki_hint);
		flag = f2fs_force_buffered_io(this, iocb, from) ?
					F2FS_GET_BLOCK_PRE_AIO : F2FS_GET_BLOCK_PRE_DIO;
		goto map_blocks;
	}
	if (iocb->ki_pos + iov_iter_count(from) > MAX_INLINE_DATA(this)) 
	{
		err = this->f2fs_convert_inline_inode();
		if (err) return err;
	}
	if (f2fs_has_inline_data(this))	return err;
	flag = F2FS_GET_BLOCK_PRE_AIO;

map_blocks:
	err = f2fs_map_blocks(&map, 1, flag);
	if (map.m_len > 0 && err == -ENOSPC) 
	{
		if (!direct_io)	this->set_inode_flag(FI_NO_PREALLOC);
		err = 0;
	}
	return err;
}

void f2fs_do_map_lock(struct f2fs_sb_info *sbi, int flag, bool lock)
{
	if (flag == F2FS_GET_BLOCK_PRE_AIO)
	{
		if (lock)			down_read(&sbi->node_change);
		else			up_read(&sbi->node_change);
	} 
	else 
	{
		if (lock)			sbi->f2fs_lock_op();
		else			sbi->f2fs_unlock_op();
	}
}

//找到一组block的sector映射，从map.m_lblk开始，map.m_len个block。

/* f2fs_map_blocks() tries to find or build mapping relationship which maps continuous logical blocks to physical blocks, and return such info via f2fs_map_blocks structure. */
//int f2fs_map_blocks(f2fs_inode_info *iinode, struct f2fs_map_blocks *map, int create, int flag)
int f2fs_inode_info::f2fs_map_blocks(struct f2fs_map_blocks* map, int create, int flag)
{
	unsigned int maxblocks = map->m_len;
	dnode_of_data dn;
//	f2fs_sb_info *sbi = F2FS_I_SB(iinode);
	int mode = map->m_may_create ? ALLOC_NODE : LOOKUP_NODE;
	pgoff_t pgofs, end_offset, end;
	int err = 0, ofs = 1;
	unsigned int ofs_in_node, last_ofs_in_node;
	blkcnt_t prealloc;
	extent_info ei = {0,0,0};
	block_t blkaddr;
	unsigned int start_pgofs;

	if (!maxblocks)		return 0;

	map->m_len = 0;
	map->m_flags = 0;

	/* it only supports block size == page size */
	pgofs =	(pgoff_t)map->m_lblk;	// 逻辑block
	end = pgofs + maxblocks;

	if (!create && f2fs_lookup_extent_cache(this, pgofs, &ei)) 
	{
		if (f2fs_lfs_mode(m_sbi) && flag == F2FS_GET_BLOCK_DIO &&	map->m_may_create)		goto next_dnode;

		map->m_pblk = ei.blk + pgofs - ei.fofs;
		map->m_len = min((pgoff_t)maxblocks, ei.fofs + ei.len - pgofs);
		map->m_flags = F2FS_MAP_MAPPED;
		if (map->m_next_extent)		*map->m_next_extent = pgofs + map->m_len;

		/* for hardware encryption, but to avoid potential issue in future */
		if (flag == F2FS_GET_BLOCK_DIO)	f2fs_wait_on_block_writeback_range(this, map->m_pblk, map->m_len);
		goto out;
	}

next_dnode:
	while (1)	// for each dnode
	{
		if (map->m_may_create)	f2fs_do_map_lock(m_sbi, flag, true);

		/* When reading holes, we need its node page */
		dn.set_new_dnode(this, NULL, NULL, 0);
		// 逻辑块pgofs转换为物理块地址，放入dn
		err = f2fs_get_dnode_of_data(&dn, pgofs, mode);
		if (err)
		{
			if (flag == F2FS_GET_BLOCK_BMAP)		map->m_pblk = 0;
			if (err == -ENOENT)
			{
				err = 0;
				if (map->m_next_pgofs)		*map->m_next_pgofs = f2fs_get_next_page_offset(&dn, pgofs);
				if (map->m_next_extent)		*map->m_next_extent = f2fs_get_next_page_offset(&dn, pgofs);
			}
			goto unlock_out;
		}

		start_pgofs = pgofs;
		prealloc = 0;
		last_ofs_in_node = ofs_in_node = dn.ofs_in_node;
		end_offset = ADDRS_PER_PAGE(dn.node_page, this);

		//next_block:
		while (1)		// for each block
		{
			blkaddr = f2fs_data_blkaddr(&dn);

			if (__is_valid_data_blkaddr(blkaddr) && !m_sbi->f2fs_is_valid_blkaddr(blkaddr, DATA_GENERIC_ENHANCE))
			{
				err = -EFSCORRUPTED;
				goto sync_out;
			}

			if (__is_valid_data_blkaddr(blkaddr))
			{
				/* use out-place-update for driect IO under LFS mode */
				if (f2fs_lfs_mode(m_sbi) && flag == F2FS_GET_BLOCK_DIO && map->m_may_create)
				{
					err = __allocate_data_block(&dn, map->m_seg_type);
					if (err)		goto sync_out;
					blkaddr = dn.data_blkaddr;
					set_inode_flag(FI_APPEND_WRITE);
				}
			}
			else
			{
				if (create)
				{
					if (unlikely(m_sbi->f2fs_cp_error()))
					{
						err = -EIO;
						goto sync_out;
					}
					if (flag == F2FS_GET_BLOCK_PRE_AIO)
					{
						if (blkaddr == NULL_ADDR)
						{
							prealloc++;
							last_ofs_in_node = dn.ofs_in_node;
						}
					}
					else
					{
						WARN_ON(flag != F2FS_GET_BLOCK_PRE_DIO && flag != F2FS_GET_BLOCK_DIO);
						err = __allocate_data_block(&dn, map->m_seg_type);
						if (!err)	set_inode_flag(FI_APPEND_WRITE);
					}
					if (err)	goto sync_out;
					map->m_flags |= F2FS_MAP_NEW;
					blkaddr = dn.data_blkaddr;
				}
				else
				{
					if (flag == F2FS_GET_BLOCK_BMAP)
					{
						map->m_pblk = 0;
						goto sync_out;
					}
					if (flag == F2FS_GET_BLOCK_PRECACHE)				goto sync_out;
					if (flag == F2FS_GET_BLOCK_FIEMAP && blkaddr == NULL_ADDR)
					{
						if (map->m_next_pgofs)		*map->m_next_pgofs = pgofs + 1;
						goto sync_out;
					}
					if (flag != F2FS_GET_BLOCK_FIEMAP)
					{	/* for defragment case */
						if (map->m_next_pgofs)	*map->m_next_pgofs = pgofs + 1;
						goto sync_out;
					}
				}
			}

			if (flag == F2FS_GET_BLOCK_PRE_AIO)		goto skip;

			if (map->m_len == 0)
			{	/* preallocated unwritten block should be mapped for fiemap. */
				if (blkaddr == NEW_ADDR)	map->m_flags |= F2FS_MAP_UNWRITTEN;
				map->m_flags |= F2FS_MAP_MAPPED;
				map->m_pblk = blkaddr;	// 设置物理地址
				map->m_len = 1;
			}
			else if ((map->m_pblk != NEW_ADDR && blkaddr == (map->m_pblk + ofs)) ||
				(map->m_pblk == NEW_ADDR && blkaddr == NEW_ADDR) || flag == F2FS_GET_BLOCK_PRE_DIO)
			{
				ofs++;
				map->m_len++;
			}
			else { goto sync_out; }

		skip:
			dn.ofs_in_node++;
			pgofs++;

			/* preallocate blocks in batch for one dnode page */
			if (flag == F2FS_GET_BLOCK_PRE_AIO && (pgofs == end || dn.ofs_in_node == end_offset))
			{
				dn.ofs_in_node = ofs_in_node;
				err = f2fs_reserve_new_blocks(&dn, prealloc);
				if (err)	goto sync_out;

				map->m_len += dn.ofs_in_node - ofs_in_node;
				if (prealloc && dn.ofs_in_node != last_ofs_in_node + 1)
				{
					err = -ENOSPC;
					goto sync_out;
				}
				dn.ofs_in_node = end_offset;
			}

			if (pgofs >= end)		goto sync_out;
			//		else if (dn.ofs_in_node < end_offset)		goto next_block;
			if (dn.ofs_in_node >= end_offset) break;
		}

		if (flag == F2FS_GET_BLOCK_PRECACHE)
		{
			if (map->m_flags & F2FS_MAP_MAPPED)
			{
				unsigned int ofs = start_pgofs - map->m_lblk;

				f2fs_update_extent_cache_range(&dn, start_pgofs, map->m_pblk + ofs, map->m_len - ofs);
			}
		}

		f2fs_put_dnode(&dn);

		if (map->m_may_create)
		{
			f2fs_do_map_lock(m_sbi, flag, false);
			m_sbi->f2fs_balance_fs(dn.node_changed);
		}
//		goto next_dnode;
	}

sync_out:

	/* for hardware encryption, but to avoid potential issue in future */
	if (flag == F2FS_GET_BLOCK_DIO && map->m_flags & F2FS_MAP_MAPPED)
		f2fs_wait_on_block_writeback_range(this, map->m_pblk, map->m_len);

	if (flag == F2FS_GET_BLOCK_PRECACHE) 
	{
		if (map->m_flags & F2FS_MAP_MAPPED) 
		{
			unsigned int ofs = start_pgofs - map->m_lblk;
			f2fs_update_extent_cache_range(&dn,	start_pgofs, map->m_pblk + ofs,	map->m_len - ofs);
		}
		if (map->m_next_extent)	*map->m_next_extent = pgofs + 1;
	}
	f2fs_put_dnode(&dn);
unlock_out:
	if (map->m_may_create)
	{
		f2fs_do_map_lock(m_sbi, flag, false);
		m_sbi->f2fs_balance_fs(dn.node_changed);
	}
out:
	//trace_f2fs_map_blocks(this, map, err);
	return err;
}

bool f2fs_overwrite_io(f2fs_inode_info *inode, loff_t pos, size_t len)
{
	struct f2fs_map_blocks map;
	block_t last_lblk;
	int err;

	if (pos + len > i_size_read(inode))
		return false;

	map.m_lblk = F2FS_BYTES_TO_BLK(pos);
	map.m_next_pgofs = NULL;
	map.m_next_extent = NULL;
	map.m_seg_type = NO_CHECK_TYPE;
	map.m_may_create = false;
	last_lblk = F2FS_BLK_ALIGN(pos + len);

	while (map.m_lblk < last_lblk) {
		map.m_len = last_lblk - map.m_lblk;
		err = inode->f2fs_map_blocks(&map, 0, F2FS_GET_BLOCK_DEFAULT);
		if (err || map.m_len == 0)
			return false;
		map.m_lblk += map.m_len;
	}
	return true;
}

static inline u64 bytes_to_blks(struct inode *inode, u64 bytes)
{
	return (bytes >> inode->i_blkbits);
}


static inline u64 blks_to_bytes(struct inode *inode, u64 blks)
{
	return (blks << inode->i_blkbits);
}

#if 0 //<TODO>

static int __get_data_block(struct inode *inode, sector_t iblock,
			struct buffer_head *bh, int create, int flag,
			pgoff_t *next_pgofs, int seg_type, bool may_write)
{
	struct f2fs_map_blocks map;
	int err;

	map.m_lblk = iblock;
	map.m_len = bytes_to_blks(inode, bh->b_size);
	map.m_next_pgofs = next_pgofs;
	map.m_next_extent = NULL;
	map.m_seg_type = seg_type;
	map.m_may_create = may_write;

	err = f2fs_map_blocks(inode, &map, create, flag);
	if (!err) {
		map_bh(bh, inode->i_sb, map.m_pblk);
		bh->b_state = (bh->b_state & ~F2FS_MAP_FLAGS) | map.m_flags;
		bh->b_size = blks_to_bytes(inode, map.m_len);
	}
	return err;
}

static int get_data_block_dio_write(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create)
{
	return __get_data_block(inode, iblock, bh_result, create,
				F2FS_GET_BLOCK_DIO, NULL,
				f2fs_rw_hint_to_seg_type(inode->i_write_hint),
				true);
}

static int get_data_block_dio(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create)
{
	return __get_data_block(inode, iblock, bh_result, create,
				F2FS_GET_BLOCK_DIO, NULL,
				f2fs_rw_hint_to_seg_type(inode->i_write_hint),
				false);
}

static int f2fs_xattr_fiemap(struct inode *inode,
				struct fiemap_extent_info *fieinfo)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct page *page;
	struct node_info ni;
	__u64 phys = 0, len;
	__u32 flags;
	nid_t xnid = F2FS_I(inode)->i_xattr_nid;
	int err = 0;

	if (f2fs_has_inline_xattr(inode)) {
		int offset;

		page = f2fs_grab_cache_page(NODE_MAPPING(sbi),
						inode->i_ino, false);
		if (!page)
			return -ENOMEM;

		err = NM_I(sbi)->f2fs_get_node_info( inode->i_ino, &ni);
		if (err) {
			f2fs_put_page(page, 1);
			return err;
		}

		phys = blks_to_bytes(inode, ni.blk_addr);
		offset = offsetof(struct f2fs_inode, i_addr) + sizeof(__le32) * (DEF_ADDRS_PER_INODE - F2FS_I(inode)->get_inline_xattr_addrs());

		phys += offset;
		len = inline_xattr_size(inode);

		f2fs_put_page(page, 1);

		flags = FIEMAP_EXTENT_DATA_INLINE | FIEMAP_EXTENT_NOT_ALIGNED;

		if (!xnid)
			flags |= FIEMAP_EXTENT_LAST;

		err = fiemap_fill_next_extent(fieinfo, 0, phys, len, flags);
		trace_f2fs_fiemap(inode, 0, phys, len, flags, err);
		if (err || err == 1)
			return err;
	}

	if (xnid) {
		page = f2fs_grab_cache_page(NODE_MAPPING(sbi), xnid, false);
		if (!page)
			return -ENOMEM;

		err = NM_I(sbi)->f2fs_get_node_info( xnid, &ni);
		if (err) {
			f2fs_put_page(page, 1);
			return err;
		}

		phys = blks_to_bytes(inode, ni.blk_addr);
		len = inode->i_sb->s_blocksize;

		f2fs_put_page(page, 1);

		flags = FIEMAP_EXTENT_LAST;
	}

	if (phys) {
		err = fiemap_fill_next_extent(fieinfo, 0, phys, len, flags);
		trace_f2fs_fiemap(inode, 0, phys, len, flags, err);
	}

	return (err < 0 ? err : 0);
}

static loff_t max_inode_blocks(struct inode *inode)
{
	loff_t result = ADDRS_PER_INODE(inode);
	loff_t leaf_count = ADDRS_PER_BLOCK(inode);

	/* two direct node blocks */
	result += (leaf_count * 2);

	/* two indirect node blocks */
	leaf_count *= NIDS_PER_BLOCK;
	result += (leaf_count * 2);

	/* one double indirect node block */
	leaf_count *= NIDS_PER_BLOCK;
	result += leaf_count;

	return result;
}

int f2fs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len)
{
	struct f2fs_map_blocks map;
	sector_t start_blk, last_blk;
	pgoff_t next_pgofs;
	u64 logical = 0, phys = 0, size = 0;
	u32 flags = 0;
	int ret = 0;
	bool compr_cluster = false;
	unsigned int cluster_size = F2FS_I(inode)->i_cluster_size;
	loff_t maxbytes;

	if (fieinfo->fi_flags & FIEMAP_FLAG_CACHE) {
		ret = f2fs_precache_extents(inode);
		if (ret)
			return ret;
	}

	ret = fiemap_prep(inode, fieinfo, start, &len, FIEMAP_FLAG_XATTR);
	if (ret)
		return ret;

	inode_lock(inode);

	maxbytes = max_file_blocks(inode) << F2FS_BLKSIZE_BITS;
	if (start > maxbytes) {
		ret = -EFBIG;
		goto out;
	}

	if (len > maxbytes || (maxbytes - len) < start)
		len = maxbytes - start;

	if (fieinfo->fi_flags & FIEMAP_FLAG_XATTR) {
		ret = f2fs_xattr_fiemap(inode, fieinfo);
		goto out;
	}

	if (f2fs_has_inline_data(inode) || inode->f2fs_has_inline_dentry()) {
		ret = f2fs_inline_data_fiemap(inode, fieinfo, start, len);
		if (ret != -EAGAIN)
			goto out;
	}

	if (bytes_to_blks(inode, len) == 0)
		len = blks_to_bytes(inode, 1);

	start_blk = bytes_to_blks(inode, start);
	last_blk = bytes_to_blks(inode, start + len - 1);

next:
	memset(&map, 0, sizeof(map));
	map.m_lblk = start_blk;
	map.m_len = bytes_to_blks(inode, len);
	map.m_next_pgofs = &next_pgofs;
	map.m_seg_type = NO_CHECK_TYPE;

	if (compr_cluster)
		map.m_len = cluster_size - 1;

	ret = f2fs_map_blocks(inode, &map, 0, F2FS_GET_BLOCK_FIEMAP);
	if (ret)
		goto out;

	/* HOLE */
	if (!(map.m_flags & F2FS_MAP_FLAGS)) {
		start_blk = next_pgofs;

		if (blks_to_bytes(inode, start_blk) < blks_to_bytes(inode,
						max_inode_blocks(inode)))
			goto prep_next;

		flags |= FIEMAP_EXTENT_LAST;
	}

	if (size) {
		flags |= FIEMAP_EXTENT_MERGED;
		if (IS_ENCRYPTED(inode))
			flags |= FIEMAP_EXTENT_DATA_ENCRYPTED;

		ret = fiemap_fill_next_extent(fieinfo, logical,
				phys, size, flags);
		trace_f2fs_fiemap(inode, logical, phys, size, flags, ret);
		if (ret)
			goto out;
		size = 0;
	}

	if (start_blk > last_blk)
		goto out;

	if (compr_cluster) {
		compr_cluster = false;


		logical = blks_to_bytes(inode, start_blk - 1);
		phys = blks_to_bytes(inode, map.m_pblk);
		size = blks_to_bytes(inode, cluster_size);

		flags |= FIEMAP_EXTENT_ENCODED;

		start_blk += cluster_size - 1;

		if (start_blk > last_blk)
			goto out;

		goto prep_next;
	}

	if (map.m_pblk == COMPRESS_ADDR) {
		compr_cluster = true;
		start_blk++;
		goto prep_next;
	}

	logical = blks_to_bytes(inode, start_blk);
	phys = blks_to_bytes(inode, map.m_pblk);
	size = blks_to_bytes(inode, map.m_len);
	flags = 0;
	if (map.m_flags & F2FS_MAP_UNWRITTEN)
		flags = FIEMAP_EXTENT_UNWRITTEN;

	start_blk += bytes_to_blks(inode, size);

prep_next:
	cond_resched();
	if (fatal_signal_pending(current))
		ret = -EINTR;
	else
		goto next;
out:
	if (ret == 1)
		ret = 0;

	inode_unlock(inode);
	return ret;
}
#endif //<TODO>

static inline loff_t f2fs_readpage_limit(f2fs_inode_info *inode)
{
	if (/*IS_ENABLED(CONFIG_FS_VERITY) &&*/ (IS_VERITY(inode) || f2fs_verity_in_progress(inode)))
		return inode->i_sb->s_maxbytes;
	return i_size_read(inode);
}


// 

static int f2fs_read_single_page(f2fs_inode_info *inode, page *ppage, unsigned nr_pages, struct f2fs_map_blocks *map, bio **bio_ret, sector_t *last_block_in_bio, bool is_readahead)
{
	bio *bbio = *bio_ret;
	const unsigned blocksize = blks_to_bytes(inode, 1);
	sector_t block_in_file;
	sector_t last_block;
	sector_t last_block_in_file;
	sector_t block_nr;
	int ret = 0;

	f2fs_sb_info* sbi = F2FS_I_SB(inode);

	block_in_file = (sector_t)page_index(ppage);
	last_block = block_in_file + nr_pages;
	last_block_in_file = bytes_to_blks(inode, f2fs_readpage_limit(inode) + blocksize - 1);
	if (last_block > last_block_in_file) last_block = last_block_in_file;

	/* just zeroing out page which is beyond EOF */
	if (block_in_file >= last_block)		goto zero_out;
	/* Map blocks using the previous result first.	 */
	if ((map->m_flags & F2FS_MAP_MAPPED) &&	block_in_file > map->m_lblk && block_in_file < (map->m_lblk + map->m_len))
		goto got_it;

	/* Then do more f2fs_map_blocks() calls until we are done with this page. */
	map->m_lblk = block_in_file;
	map->m_len = last_block - block_in_file;

	ret = inode->f2fs_map_blocks(map, 0, F2FS_GET_BLOCK_DEFAULT);
	if (ret)	goto out;
got_it:
	if ((map->m_flags & F2FS_MAP_MAPPED)) 
	{
		block_nr = map->m_pblk + block_in_file - map->m_lblk;
		SetPageMappedToDisk(ppage);

		//if (!PageUptodate(ppage) && (!PageSwapCache(ppage) /*&& !cleancache_get_page(ppage)*/)) 
		//{
		//	SetPageUptodate(ppage);
		//	goto confused;
		//}

		if (!sbi->f2fs_is_valid_blkaddr(block_nr, DATA_GENERIC_ENHANCE_READ))
		{
			ret = -EFSCORRUPTED;
			goto out;
		}
	}
	else 
	{
zero_out:
		zero_user_segment(ppage, 0, PAGE_SIZE);
		// fsverify_verify_page()检查这个page是否是pagecahce，并且已经lock住。
		if (f2fs_need_verity(inode, ppage->index) /*&& !fsverity_verify_page(ppage)*/) 
		{
			ret = -EIO;
			goto out;
		}
		if (!PageUptodate(ppage))		SetPageUptodate(ppage);
		unlock_page(ppage);
		goto out;
	}

	/* This page will go to BIO.  Do we need to send this BIO off first? */
	// 如果从file_mpage_read_pages()，调用bio为空
	if (bbio && (!inode->m_sbi->page_is_mergeable(bbio, *last_block_in_bio, block_nr) ||
		    !f2fs_crypt_mergeable_bio(bbio, inode, ppage->index, NULL)))
	{
submit_and_realloc:
		sbi->__submit_bio(bbio, DATA);
		bbio = NULL;
	}
	if (bbio == NULL)
	{
		bbio = sbi->f2fs_grab_read_bio(inode, block_nr, nr_pages,	is_readahead ? REQ_RAHEAD : 0, ppage->index,	false);
		if (IS_ERR(bbio)) 
		{
			ret = PTR_ERR(bbio);
			bbio = NULL;
			goto out;
		}
	}

	/* If the page is under writeback, we need to wait for its completion to see the correct decrypted data. */
	f2fs_wait_on_block_writeback(inode, block_nr);

	if (bio_add_page(bbio, ppage, blocksize, 0) < blocksize)	goto submit_and_realloc;

	F2FS_I_SB(inode)->inc_page_count(F2FS_RD_DATA);
	sbi->f2fs_update_iostat(FS_DATA_READ_IO, F2FS_BLKSIZE);
	ClearPageError(ppage);
	*last_block_in_bio = block_nr;
	goto out;
//confused:
	if (bbio) 
	{
		sbi->__submit_bio(bbio, DATA);
		bbio = NULL;
	}
	unlock_page(ppage);
out:
	*bio_ret = bbio;
	return ret;
}

#if 0 //<TODO>

#ifdef CONFIG_F2FS_FS_COMPRESSION
int f2fs_read_multi_pages(struct compress_ctx *cc, struct bio **bio_ret,
				unsigned nr_pages, sector_t *last_block_in_bio,
				bool is_readahead, bool for_write)
{
	struct dnode_of_data dn;
	struct inode *inode = cc->inode;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct bio *bio = *bio_ret;
	unsigned int start_idx = cc->cluster_idx << cc->log_cluster_size;
	sector_t last_block_in_file;
	const unsigned blocksize = blks_to_bytes(inode, 1);
	struct decompress_io_ctx *dic = NULL;
	int i;
	int ret = 0;

	f2fs_bug_on(sbi, f2fs_cluster_is_empty(cc));

	last_block_in_file = bytes_to_blks(inode,
			f2fs_readpage_limit(inode) + blocksize - 1);

	/* get rid of pages beyond EOF */
	for (i = 0; i < cc->cluster_size; i++) {
		struct page *page = cc->rpages[i];

		if (!page)
			continue;
		if ((sector_t)page->index >= last_block_in_file) {
			zero_user_segment(page, 0, PAGE_SIZE);
			if (!PageUptodate(page))
				SetPageUptodate(page);
		} else if (!PageUptodate(page)) {
			continue;
		}
		unlock_page(page);
		cc->rpages[i] = NULL;
		cc->nr_rpages--;
	}

	/* we are done since all pages are beyond EOF */
	if (f2fs_cluster_is_empty(cc))
		goto out;

	dn.set_new_dnode(inode, NULL, NULL, 0);
	ret = f2fs_get_dnode_of_data(&dn, start_idx, LOOKUP_NODE);
	if (ret)
		goto out;

	f2fs_bug_on(sbi, dn.data_blkaddr != COMPRESS_ADDR);

	for (i = 1; i < cc->cluster_size; i++) {
		block_t blkaddr;

		blkaddr = data_blkaddr(dn.inode, dn.node_page,
						dn.ofs_in_node + i);

		if (!__is_valid_data_blkaddr(blkaddr))
			break;

		if (!sbi->f2fs_is_valid_blkaddr(blkaddr, DATA_GENERIC)) {
			ret = -EFAULT;
			goto out_put_dnode;
		}
		cc->nr_cpages++;
	}

	/* nothing to decompress */
	if (cc->nr_cpages == 0) {
		ret = 0;
		goto out_put_dnode;
	}

	dic = f2fs_alloc_dic(cc);
	if (IS_ERR(dic)) {
		ret = PTR_ERR(dic);
		goto out_put_dnode;
	}

	for (i = 0; i < dic->nr_cpages; i++) {
		struct page *page = dic->cpages[i];
		block_t blkaddr;
		struct bio_post_read_ctx *ctx;

		blkaddr = data_blkaddr(dn.inode, dn.node_page,
						dn.ofs_in_node + i + 1);

		if (bio && (!page_is_mergeable(sbi, bio,
					*last_block_in_bio, blkaddr) ||
		    !f2fs_crypt_mergeable_bio(bio, inode, page->index, NULL))) {
submit_and_realloc:
			sbi->m_fs->__submit_bio( bio, DATA);
			bio = NULL;
		}

		if (!bio) {
			bio = f2fs_grab_read_bio(inode, blkaddr, nr_pages,
					is_readahead ? REQ_RAHEAD : 0,
					page->index, for_write);
			if (IS_ERR(bio)) {
				ret = PTR_ERR(bio);
				f2fs_decompress_end_io(dic, ret);
				f2fs_put_dnode(&dn);
				*bio_ret = NULL;
				return ret;
			}
		}

		f2fs_wait_on_block_writeback(inode, blkaddr);

		if (bio_add_page(bio, page, blocksize, 0) < blocksize)
			goto submit_and_realloc;

		ctx = bio->bi_private;
		ctx->enabled_steps |= STEP_DECOMPRESS;
		refcount_inc(&dic->refcnt);

		sbi->inc_page_count(F2FS_RD_DATA);
		sbi->f2fs_update_iostat(FS_DATA_READ_IO, F2FS_BLKSIZE);
		sbi->f2fs_update_iostat(FS_CDATA_READ_IO, F2FS_BLKSIZE);
		ClearPageError(page);
		*last_block_in_bio = blkaddr;
	}

	f2fs_put_dnode(&dn);

	*bio_ret = bio;
	return 0;

out_put_dnode:
	f2fs_put_dnode(&dn);
out:
	for (i = 0; i < cc->cluster_size; i++) {
		if (cc->rpages[i]) {
			ClearPageUptodate(cc->rpages[i]);
			ClearPageError(cc->rpages[i]);
			unlock_page(cc->rpages[i]);
		}
	}
	*bio_ret = bio;
	return ret;
}
#endif

#endif	//<TODO>

/* This function was originally taken from fs/mpage.c, and customized for f2fs.
 * Major change was from block_size == page_size in f2fs by default. */
static int f2fs_mpage_readpages(f2fs_inode_info*inode, readahead_control *rac, page *ppage)
{
	f2fs_sb_info* sbi = F2FS_I_SB(inode);

	struct bio *bio = NULL;
	sector_t last_block_in_bio = 0;
	struct f2fs_map_blocks map;
#ifdef CONFIG_F2FS_FS_COMPRESSION
	struct compress_ctx cc = {
		.inode = inode,
		.log_cluster_size = F2FS_I(inode)->i_log_cluster_size,
		.cluster_size = F2FS_I(inode)->i_cluster_size,
		.cluster_idx = NULL_CLUSTER,
		.rpages = NULL,
		.cpages = NULL,
		.nr_rpages = 0,
		.nr_cpages = 0,
	};
#endif
	// 一次read ahead的页数
	unsigned nr_pages = rac ? readahead_count(rac) : 1;
	unsigned max_nr_pages = nr_pages;
	int ret = 0;

	map.m_pblk = 0;
	map.m_lblk = 0;
	map.m_len = 0;
	map.m_flags = 0;
	map.m_next_pgofs = NULL;
	map.m_next_extent = NULL;
	map.m_seg_type = NO_CHECK_TYPE;
	map.m_may_create = false;

	for (; nr_pages; nr_pages--) 
	{
		if (rac) 
		{	// 返回下一个要读取的 page
			ppage = readahead_page(rac);
//			prefetchw(&page->flags);
		}

#ifdef CONFIG_F2FS_FS_COMPRESSION
		if (f2fs_compressed_file(inode)) {
			/* there are remained comressed pages, submit them */
			if (!f2fs_cluster_can_merge_page(&cc, ppage->index)) {
				ret = f2fs_read_multi_pages(&cc, &bio,
							max_nr_pages,
							&last_block_in_bio,
							rac != NULL, false);
				f2fs_destroy_compress_ctx(&cc, false);
				if (ret)
					goto set_error_page;
			}
			ret = f2fs_is_compressed_cluster(inode, ppage->index);
			if (ret < 0)
				goto set_error_page;
			else if (!ret)
				goto read_single_page;

			ret = f2fs_init_compress_ctx(&cc);
			if (ret)
				goto set_error_page;

			f2fs_compress_ctx_add_page(&cc, ppage);

			goto next_page;
		}
read_single_page:
#endif
		// 读取指定的page，对读取位置的描述在bio中
		ret = f2fs_read_single_page(inode, ppage, max_nr_pages, &map, &bio, &last_block_in_bio, rac);
		if (ret) 
		{
#ifdef CONFIG_F2FS_FS_COMPRESSION
set_error_page:
#endif
			SetPageError(ppage);
			zero_user_segment(ppage, 0, PAGE_SIZE);
			unlock_page(ppage);
		}
#ifdef CONFIG_F2FS_FS_COMPRESSION
next_page:
#endif
		if (rac)
		{
			TRACK_PAGE(ppage, L"put page");
			ppage->put_page();
		}

#ifdef CONFIG_F2FS_FS_COMPRESSION
		if (f2fs_compressed_file(inode)) {
			/* last page */
			if (nr_pages == 1 && !f2fs_cluster_is_empty(&cc)) {
				ret = f2fs_read_multi_pages(&cc, &bio,
							max_nr_pages,
							&last_block_in_bio,
							rac != NULL, false);
				f2fs_destroy_compress_ctx(&cc, false);
			}
		}
#endif
	}
	if (bio) sbi->__submit_bio(bio, DATA);
	return ret;
}

//static int f2fs_read_data_page(struct file *file, struct page *page)
int Cf2fsDataMapping::read_page(file * file, page * page)
{
	//inode *ii = page_file_mapping(page)->host;
	f2fs_inode_info* inode = F2FS_I(host);
	int ret = -EAGAIN;
//	trace_f2fs_readpage(page, DATA);
	if (!f2fs_is_compress_backend_ready(inode)) 
	{
		unlock_page(page);
		return -EOPNOTSUPP;
	}

	/* If the file has inline data, try to read it directly */
	if (f2fs_has_inline_data(inode))		ret = f2fs_read_inline_data(inode, page);
	if (ret == -EAGAIN)		ret = f2fs_mpage_readpages(inode, NULL, page);
	return ret;
}


//static void f2fs_readahead(struct readahead_control *rac)
void Cf2fsDataMapping::read_ahead(readahead_control * rac)
{
//	inode *iinode = rac->mapping->host;
	f2fs_inode_info* fi = F2FS_I(host);
//	trace_f2fs_readpages(inode, readahead_index(rac), readahead_count(rac));

	if (!f2fs_is_compress_backend_ready(fi))		return;
	/* If the file has inline data, skip readpages */
	if (f2fs_has_inline_data(fi))		return;
	f2fs_mpage_readpages(fi, rac, NULL);
}

int f2fs_encrypt_one_page(struct f2fs_io_info *fio)
{
#if 0 //TODO not support encryption
	struct inode *inode = fio->page->mapping->host;
	struct page *mpage, *page;
	gfp_t gfp_flags = GFP_NOFS;

	if (!f2fs_encrypted_file(inode))	return 0;

	page = fio->compressed_page ? fio->compressed_page : fio->page;

	/* wait for GCed page writeback via META_MAPPING */
	f2fs_wait_on_block_writeback(inode, fio->old_blkaddr);

	if (fscrypt_inode_uses_inline_crypto(inode))
		return 0;

retry_encrypt:
	fio->encrypted_page = fscrypt_encrypt_pagecache_blocks(page, PAGE_SIZE, 0, gfp_flags);
	if (IS_ERR(fio->encrypted_page)) {
		/* flush pending IOs and wait for a while in the ENOMEM case */
		if (PTR_ERR(fio->encrypted_page) == -ENOMEM) {
			f2fs_flush_merged_writes(fio->sbi);
			congestion_wait(BLK_RW_ASYNC, DEFAULT_IO_TIMEOUT);
			gfp_flags |= __GFP_NOFAIL;
			goto retry_encrypt;
		}
		return PTR_ERR(fio->encrypted_page);
	}

	mpage = find_lock_page(META_MAPPING(fio->sbi), fio->old_blkaddr);
	if (mpage) {
		if (PageUptodate(mpage))
			memcpy(page_address(mpage),
				page_address(fio->encrypted_page), PAGE_SIZE);
		f2fs_put_page(mpage, 1);
	}
#endif
	return 0;
}

static inline bool check_inplace_update_policy(f2fs_inode_info *inode,	f2fs_io_info *fio)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	unsigned int policy = sbi->SM_I()->ipu_policy;

	if (policy & (0x1 << F2FS_IPU_FORCE))
		return true;
	if (policy & (0x1 << F2FS_IPU_SSR) && f2fs_need_SSR(sbi))
		return true;
	if (policy & (0x1 << F2FS_IPU_UTIL) && utilization(sbi) > sbi->SM_I()->min_ipu_util)
		return true;
	if (policy & (0x1 << F2FS_IPU_SSR_UTIL) && f2fs_need_SSR(sbi) && utilization(sbi) > sbi->SM_I()->min_ipu_util)
		return true;

	/* IPU for rewrite async pages	 */
	if (policy & (0x1 << F2FS_IPU_ASYNC) && fio && fio->op == REQ_OP_WRITE && !(fio->op_flags & REQ_SYNC) && !IS_ENCRYPTED(inode))
		return true;

	/* this is only set during fdatasync */
	if (policy & (0x1 << F2FS_IPU_FSYNC) && is_inode_flag_set(inode, FI_NEED_IPU))
		return true;

	if (unlikely(fio && sbi->is_sbi_flag_set( SBI_CP_DISABLED) && !f2fs_is_checkpointed_data(sbi, fio->old_blkaddr)))
		return true;

	return false;
}

bool f2fs_should_update_inplace(f2fs_inode_info *inode, struct f2fs_io_info *fio)
{
	if (f2fs_is_pinned_file(inode))	return true;

	/* if this is cold file, we should overwrite to avoid fragmentation */
	if (file_is_cold(inode))		return true;

	return check_inplace_update_policy(inode, fio);
}

bool f2fs_should_update_outplace(f2fs_inode_info *inode, struct f2fs_io_info *fio)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	if (f2fs_lfs_mode(sbi))			return true;
	if (S_ISDIR(inode->i_mode))		return true;
	if (IS_NOQUOTA(inode))			return true;
	if (f2fs_is_atomic_file(inode))	return true;
	if (fio) 
	{
		if (is_cold_data(fio->page))			return true;
		if (IS_ATOMIC_WRITTEN_PAGE(fio->page))			return true;
		if (unlikely(sbi->is_sbi_flag_set( SBI_CP_DISABLED) &&
			f2fs_is_checkpointed_data(sbi, fio->old_blkaddr)))
			return true;
	}
	return false;
}

static inline bool need_inplace_update(struct f2fs_io_info *fio)
{
	inode *iinode = fio->page->mapping->host;
	f2fs_inode_info* fi = F2FS_I(iinode);

	if (f2fs_should_update_outplace(fi, fio))	return false;
	return f2fs_should_update_inplace(fi, fio);
}


int f2fs_do_write_data_page(f2fs_io_info *fio)
{
	LOG_DEBUG_(1,L"[IO] page=%p, flag=%X, write date page, ino=%d", fio->page, fio->page->flags, fio->ino);
	page *ppage = fio->page;
	inode *iinode = ppage->mapping->host;
	f2fs_inode_info* fi = F2FS_I(iinode);
	struct dnode_of_data dn;
	struct extent_info ei = {0,0,0};
	struct node_info ni;
	bool ipu_force = false;
	int err = 0;

	dn.set_new_dnode(iinode, NULL, NULL, 0);
	if (need_inplace_update(fio) &&	f2fs_lookup_extent_cache(fi, ppage->index, &ei))
	{ // Inplace update
		fio->old_blkaddr = ei.blk + ppage->index - ei.fofs;
		if (!fio->sbi->f2fs_is_valid_blkaddr(fio->old_blkaddr, DATA_GENERIC_ENHANCE))
			return -EFSCORRUPTED;
		ipu_force = true;
		fio->need_lock = LOCK_DONE;
		goto got_it;
	}

	// Outplace update
	/* Deadlock due to between page->lock and f2fs_lock_op */
	if (fio->need_lock == LOCK_REQ && !fio->sbi->f2fs_trylock_op())		return -EAGAIN;

	err = f2fs_get_dnode_of_data(&dn, ppage->index, LOOKUP_NODE);
	if (err)	goto out;

	fio->old_blkaddr = dn.data_blkaddr;

	/* This page is already truncated */
	if (fio->old_blkaddr == NULL_ADDR)
	{
		ClearPageUptodate(ppage);
		clear_cold_data(ppage);
		goto out_writepage;
	}

got_it:
	if (__is_valid_data_blkaddr(fio->old_blkaddr) && ! fio->sbi->f2fs_is_valid_blkaddr(fio->old_blkaddr, DATA_GENERIC_ENHANCE)) 
	{
		err = -EFSCORRUPTED;
		goto out_writepage;
	}
	/* If current allocation needs SSR, it had better in-place writes for updated data. */
	if (ipu_force || (__is_valid_data_blkaddr(fio->old_blkaddr) &&	need_inplace_update(fio))) 
	{	// Inplace write
		err = f2fs_encrypt_one_page(fio);
		if (err)		goto out_writepage;

		set_page_writeback(ppage);
		ClearPageError(ppage);
		f2fs_put_dnode(&dn);
		if (fio->need_lock == LOCK_REQ)			fio->sbi->f2fs_unlock_op();
		err = f2fs_inplace_write_data(fio);
		if (err) 
		{
			LOG_ERROR(L"[err] failed on inplace write data, err=%d", err);
			if (fscrypt_inode_uses_fs_layer_crypto(iinode))
				fscrypt_finalize_bounce_page(&fio->encrypted_page);
			if (PageWriteback(ppage))			end_page_writeback(ppage);
		} 
		else 
		{
			set_inode_flag(fi, FI_UPDATE_WRITE);
		}
//		trace_f2fs_do_write_data_page(fio->page, IPU);
		return err;
	}

	if (fio->need_lock == LOCK_RETRY) 
	{
		if (!fio->sbi->f2fs_trylock_op()) 
		{
			err = -EAGAIN;
			goto out_writepage;
		}
		fio->need_lock = LOCK_REQ;
	}

	err = NM_I(fio->sbi)->f2fs_get_node_info(dn.nid, &ni);
	if (err)	goto out_writepage;

	fio->version = ni.version;

	err = f2fs_encrypt_one_page(fio);
	if (err)		goto out_writepage;

	set_page_writeback(ppage);
	ClearPageError(ppage);

	if (fio->compr_blocks && fio->old_blkaddr == COMPRESS_ADDR)
		f2fs_i_compr_blocks_update(iinode, fio->compr_blocks - 1, false);

	/* LFS mode write path */
	f2fs_outplace_write_data(&dn, fio);
//	trace_f2fs_do_write_data_page(ppage, OPU);
	set_inode_flag(fi, FI_APPEND_WRITE);
	if (ppage->index == 0)
		set_inode_flag(fi, FI_FIRST_BLOCK_WRITTEN);
out_writepage:
	f2fs_put_dnode(&dn);
out:
	if (fio->need_lock == LOCK_REQ)		fio->sbi->f2fs_unlock_op();
	return err;
}


int f2fs_write_single_data_page(page *ppage, int *submitted, struct bio **bio, sector_t *last_block, 
		writeback_control *wbc, enum iostat_type io_type, int compr_blocks, bool allow_balance)
{
	LOG_STACK_TRACE();
	inode *iinode = ppage->mapping->host;
	f2fs_inode_info* fi = F2FS_I(iinode);
	f2fs_sb_info *sbi = fi->m_sbi;
	loff_t i_size = i_size_read(iinode);
	const pgoff_t end_index = ((unsigned long long)i_size) >> PAGE_SHIFT;
	loff_t psize = (loff_t)(ppage->index + 1) << PAGE_SHIFT;
	unsigned offset = 0;
	bool need_balance_fs = false;
	int err = 0;
	bool cp_running = false;

	struct f2fs_io_info fio;
	fio.sbi = sbi;
	fio.ino = iinode->i_ino;
	fio.type = DATA;
	fio.op = REQ_OP_WRITE;
	fio.op_flags = wbc_to_write_flags(wbc);
	fio.old_blkaddr = NULL_ADDR;
	fio.page = ppage;
	fio.encrypted_page = NULL;
	fio.submitted = false;
	fio.compr_blocks = compr_blocks;
	fio.need_lock = LOCK_RETRY;
	fio.io_type = io_type;
	fio.io_wbc = wbc;
	fio.bio = bio;
	fio.last_block = last_block;

	//trace_f2fs_writepage(ppage, DATA);

	/* we should bypass data pages to proceed the kworkder jobs */
	if (unlikely(sbi->f2fs_cp_error())) 
	{
#if 0 //TODO
		mapping_set_error(ppage->mapping, -EIO);
#else
		LOG_ERROR(L"error in page mapping: EIO");
#endif
		/* don't drop any dirty dentry pages for keeping lastest directory structure. */
		if (S_ISDIR(iinode->i_mode))		goto redirty_out;
		goto out;
	}

	if (unlikely(sbi->is_sbi_flag_set( SBI_POR_DOING)))		goto redirty_out;

	if (ppage->index < end_index ||	f2fs_verity_in_progress(fi) || compr_blocks)
		goto write;

	/* If the offset is out-of-range of file size, this page does not have to be written to disk. */
	offset = i_size & (PAGE_SIZE - 1);
	if ((ppage->index >= end_index + 1) || !offset)	goto out;

	zero_user_segment(ppage, offset, PAGE_SIZE);
write:
	if (f2fs_is_drop_cache(fi))		goto out;
	/* we should not write 0'th page having journal header */
	if (f2fs_is_volatile_file(fi) && (!ppage->index || (!wbc->for_reclaim && f2fs_available_free_memory(sbi, BASE_CHECK))))
		goto redirty_out;

	/* Dentry/quota blocks are controlled by checkpoint */
	if (S_ISDIR(iinode->i_mode) || IS_NOQUOTA(iinode))
	{
		/* We need to wait for node_write to avoid block allocation during checkpoint. This can only happen to quota writes which can cause the below discard race condition.	 */
		if (IS_NOQUOTA(iinode))		down_read(&sbi->node_write);

		fio.need_lock = LOCK_DONE;
		err = f2fs_do_write_data_page(&fio);
		LOG_DEBUG_(1, L"after do write data page, page=%p, flag=%X, writeback=%d", ppage, ppage ? ppage->flags : -1, ppage ? PageWriteback(ppage) : -1);
		LOG_DEBUG_(1, L"[bio] bio=%p called f2fs_do_write_data_page() 1", *bio);

		if (IS_NOQUOTA(iinode))		up_read(&sbi->node_write);
		goto done;
	}

	if (!wbc->for_reclaim)		need_balance_fs = true;
	else if (sbi->has_not_enough_free_secs(0, 0)) 	goto redirty_out;
	else	fi->set_inode_flag(FI_HOT_DATA);

	err = -EAGAIN;
	if (f2fs_has_inline_data(fi))
	{
		err = f2fs_write_inline_data(fi, ppage);
		if (!err) 	goto out;
	}

	if (err == -EAGAIN)
	{
		err = f2fs_do_write_data_page(&fio);
		if (err) LOG_ERROR(L"[err] failed on writing data page, err=%d", err);
		if (err == -EAGAIN) 
		{
			fio.need_lock = LOCK_REQ;
			err = f2fs_do_write_data_page(&fio);
		}
		LOG_DEBUG_(1,L"do write data page while err=EAGAIN, page=%llX, flag=%X, writeback=%d", ppage, ppage ? ppage->flags : -1, ppage ? PageWriteback(ppage) : -1);
		LOG_DEBUG_(1, L"[bio] bio=%p called f2fs_do_write_data_page() 2", *bio);
	}

	if (err) {	file_set_keep_isize(fi); } 
	else 
	{
//		LOG_DEBUG_(-1, L"spin lock for %s", fi->m_description.c_str());
		spin_lock(&fi->i_size_lock);
		if (fi->last_disk_size < psize)		fi->last_disk_size = psize;
		spin_unlock(&fi->i_size_lock);
	}

done:
	if (err && err != -ENOENT)	goto redirty_out;

out:
#ifdef DEBUG_PAGE
	LOG_TRACK(L"page.dirty", L" dec: inode=%d, page=%d, flag=%X, lock_th=%04X", fi->i_ino, ppage->index, ppage->flags, ppage->lock_th_id);
#endif
	inode_dec_dirty_pages(fi);
	if (err) 
	{
		ClearPageUptodate(ppage);
		clear_cold_data(ppage);
	}

	if (wbc->for_reclaim) {
		f2fs_submit_merged_write_cond(sbi, NULL, ppage, 0, DATA);
		clear_inode_flag(fi, FI_HOT_DATA);
		f2fs_remove_dirty_inode(fi);
		submitted = NULL;
	}
	LOG_TRACK(L"page", L" page=%p, index=%d, flag=%X, unlock after write", ppage, ppage->index, ppage->flags);
	unlock_page(ppage);
	cp_running = fi->m_sbi->cprc_info.IsRunning();
	if (!S_ISDIR(iinode->i_mode) && !IS_NOQUOTA(iinode) &&	!cp_running && allow_balance)
		sbi->f2fs_balance_fs(need_balance_fs);

	if (unlikely(sbi->f2fs_cp_error())) 
	{
		f2fs_submit_merged_write(sbi, DATA);
		f2fs_submit_merged_ipu_write(sbi, bio, NULL);
		LOG_DEBUG_(1, L"[bio] bio=%p, called f2fs_submit_merged_ipu_write()", *bio);
		submitted = NULL;
	}

	if (submitted)
		*submitted = fio.submitted ? 1 : 0;

	return 0;

redirty_out:
	redirty_page_for_writepage(wbc, ppage);
	/* pageout() in MM traslates EAGAIN, so calls handle_write_error() -> mapping_set_error() -> set_bit(AS_EIO, ...).
	 * file_write_and_wait_range() will see EIO error, which is critical to return value of fsync() followed by atomic_write failure to user. */
	if (!err || wbc->for_reclaim)	return AOP_WRITEPAGE_ACTIVATE;
	unlock_page(ppage);
	return err;
}

//static int f2fs_write_data_page(struct page *page, struct writeback_control *wbc)
int Cf2fsDataMapping::write_page(page * page, writeback_control *wbc)
{
#ifdef CONFIG_F2FS_FS_COMPRESSION
	struct inode *inode = page->mapping->host;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(inode))))
		goto out;

	if (f2fs_compressed_file(inode)) {
		if (f2fs_is_compressed_cluster(inode, page->index)) {
			redirty_page_for_writepage(wbc, page);
			return AOP_WRITEPAGE_ACTIVATE;
		}
	}
out:
#endif
#ifdef DEBUG_PAGE
	LOG_TRACK(L"page", L" page=%p, index=%d, flag=%X, lock_th=%04X write page", page, page->index, page->flags, page->lock_th_id);
#endif
	return f2fs_write_single_data_page(page, NULL, NULL, NULL, wbc, FS_DATA_IO, 0, true);
}

/* This function was copied from write_cche_pages from mm/page-writeback.c. The major change is making write step of 
cold data page separately from warm/hot data page. */
//static int f2fs_write_cache_pages(address_space *mapping, writeback_control *wbc, enum iostat_type io_type)
int Cf2fsDataMapping::f2fs_write_cache_pages(writeback_control* wbc, iostat_type io_type)
{
	LOG_STACK_TRACE();
	int ret = 0;
	int done = 0, retry = 0;
	pagevec pvec;
	struct bio *bio = NULL;
	sector_t last_block;
#ifdef CONFIG_F2FS_FS_COMPRESSION
	struct inode *inode = this->host;
	struct compress_ctx cc = {
		.inode = inode,
		.log_cluster_size = F2FS_I(inode)->i_log_cluster_size,
		.cluster_size = F2FS_I(inode)->i_cluster_size,
		.cluster_idx = NULL_CLUSTER,
		.rpages = NULL,
		.nr_rpages = 0,
		.cpages = NULL,
		.rbuf = NULL,
		.cbuf = NULL,
		.rlen = PAGE_SIZE * F2FS_I(inode)->i_cluster_size,
		.private = NULL,
	};
#endif
	int nr_pages;
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	pgoff_t done_index;
	int range_whole = 0;
	xa_mark_t tag;
	int nwritten = 0;
	int submitted = 0;
	int i;

	f2fs_inode_info* fi = F2FS_I(host);
	f2fs_sb_info *sbi = fi->m_sbi;
	pagevec_init(&pvec);

	if (get_dirty_pages(fi) <= sbi->SM_I()->min_hot_blocks) 	fi->set_inode_flag(FI_HOT_DATA);
	else		clear_inode_flag(fi, FI_HOT_DATA);

	if (wbc->range_cyclic) 
	{
		index = writeback_index; /* prev offset */
		end = -1;
	}
	else
	{
		index = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)		range_whole = 1;
	}
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)		tag = PAGECACHE_TAG_TOWRITE;
	else		tag = PAGECACHE_TAG_DIRTY;
retry:
	retry = 0;
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)	tag_pages_for_writeback(this, index, end);
	done_index = index;
	while (!done && !retry && (index <= end)) 
	{
		nr_pages = pagevec_lookup_range_tag(&pvec, this, &index, end,	tag);
		if (nr_pages == 0)		break;

		for (i = 0; i < nr_pages; i++)
		{
			page* ppage = pvec.pages[i];
			LOG_DEBUG_(1,L"[page_write] page=%p, index=%d, flag=%X, try to write page", ppage, ppage->index, ppage->flags);
			bool need_readd;
		readd:
			need_readd = false;
#ifdef CONFIG_F2FS_FS_COMPRESSION
			if (f2fs_compressed_file(inode))
			{
				ret = f2fs_init_compress_ctx(&cc);
				if (ret)
				{
					done = 1;
					break;
				}

				if (!f2fs_cluster_can_merge_page(&cc,
					ppage->index))
				{
					ret = f2fs_write_multi_pages(&cc,
						&submitted, wbc, io_type);
					if (!ret)
						need_readd = true;
					goto result;
				}

				if (unlikely(sbi->f2fs_cp_error()))
					goto lock_page;

				if (f2fs_cluster_is_empty(&cc))
				{
					void* fsdata = NULL;
					struct ppage* pagep;
					int ret2;

					ret2 = f2fs_prepare_compress_overwrite(
						inode, &pagep,
						ppage->index, &fsdata);
					if (ret2 < 0)
					{
						ret = ret2;
						done = 1;
						break;
					}
					else if (ret2 &&
						!f2fs_compress_write_end(inode,
							fsdata, ppage->index,
							1))
					{
						retry = 1;
						break;
					}
				}
				else
				{
					goto lock_page;
				}
			}
#endif
			/* give a priority to WB_SYNC threads */
			if (atomic_read(&sbi->wb_sync_req[DATA]) && wbc->sync_mode == WB_SYNC_NONE)
			{
				done = 1;
				break;
			}
#ifdef CONFIG_F2FS_FS_COMPRESSION
			lock_page :
#endif
			done_index = ppage->index;
retry_write:
//			{
				// 此处使用auto lock为避免goto的跳转。但是不清楚为什么只有在AOP_WRITEPAGE_ACTIVATE时才解除unlock
//				auto_lock<page_auto_lock> page_locker(*ppage);
			ppage->lock();
			LOG_DEBUG_(1, L"[track_page_lock] page=%p, flag=0x%X, locked", ppage, ppage->flags);
			if (unlikely(ppage->mapping != this))	{ppage->unlock();	continue;}
			if (!PageDirty(ppage))
			{
				LOG_DEBUG_(1,L"[page_write] page=%p, index=%d, flag=%X, skip = page not dirty", ppage, ppage->index, ppage->flags);
				ppage->unlock();
				continue; /* someone wrote it for us */
			}
			if (PageWriteback(ppage))
			{
				LOG_DEBUG_(1,L"[page_write] page=%p, index=%d, flag=%X, page is writing, sync=%d", ppage, ppage->index, ppage->flags, wbc->sync_mode);
				if (wbc->sync_mode != WB_SYNC_NONE) f2fs_wait_on_page_writeback(ppage, DATA, true, true);
				else								{ppage->unlock();	continue;}
			}
			if (!clear_page_dirty_for_io(ppage))	{ppage->unlock();	continue;}

#ifdef CONFIG_F2FS_FS_COMPRESSION
			if (f2fs_compressed_file(inode))
			{
				ppage->get_page();
				f2fs_compress_ctx_add_page(&cc, ppage);
				continue;
			}
#endif
			LOG_DEBUG_(1, L"[track_page_lock] page=%p, flag=0x%X, write page", ppage, ppage->flags);
			ret = f2fs_write_single_data_page(ppage, &submitted, &bio, &last_block, wbc, io_type, 0, true);
			if (ret == AOP_WRITEPAGE_ACTIVATE)		unlock_page(ppage);
#ifdef CONFIG_F2FS_FS_COMPRESSION
			result :
#endif
//			}
			nwritten += submitted;
			wbc->nr_to_write -= submitted;

			if (unlikely(ret)) 
			{
				/* keep nr_to_write, since vfs uses this to get # of written pages.	 */
				if (ret == AOP_WRITEPAGE_ACTIVATE) 
				{
					ret = 0;
					goto next;
				}
				else if (ret == -EAGAIN) 
				{
					ret = 0;
					if (wbc->sync_mode == WB_SYNC_ALL) 		{	goto retry_write;	}
					goto next;
				}
				done_index = ppage->index + 1;
				done = 1;
				break;
			}

			if (wbc->nr_to_write <= 0 && wbc->sync_mode == WB_SYNC_NONE) 
			{
				done = 1;
				break;
			}
next:
			if (need_readd)		goto readd;
		}
		pagevec_release(&pvec);
	}
#ifdef CONFIG_F2FS_FS_COMPRESSION
	/* flush remained pages in compress cluster */
	if (f2fs_compressed_file(inode) && !f2fs_cluster_is_empty(&cc)) {
		ret = f2fs_write_multi_pages(&cc, &submitted, wbc, io_type);
		nwritten += submitted;
		wbc->nr_to_write -= submitted;
		if (ret) {
			done = 1;
			retry = 0;
		}
	}
	if (f2fs_compressed_file(inode))
		f2fs_destroy_compress_ctx(&cc, false);
#endif
	if (retry) 
	{
		index = 0;
		end = -1;
		goto retry;
	}
	if (wbc->range_cyclic && !done)
		done_index = 0;
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		this->writeback_index = done_index;

	if (nwritten)	f2fs_submit_merged_write_cond(sbi, host,	NULL, 0, DATA);
	/* submit cached bio of IPU write */
	if (bio)		f2fs_submit_merged_ipu_write(sbi, &bio, NULL);

	return ret;
}

static inline bool __should_serialize_io(f2fs_inode_info *iinode, writeback_control *wbc)
{
	/* to avoid deadlock in path of data flush */
//	if (F2FS_I(iinode)->cp_task) return false;
	f2fs_sb_info* sbi = iinode->m_sbi;
	if (sbi->cprc_info.IsRunning())
	{
		LOG_DEBUG_(1,L"return due to CP thread is running");
		return false;
	}

	if (!S_ISREG(iinode->i_mode))		return false;
	if (IS_NOQUOTA(iinode))				return false;

	if (f2fs_need_compress_data(iinode))		return true;
	if (wbc->sync_mode != WB_SYNC_ALL)			return true;
	if (get_dirty_pages(iinode) >= (sbi->SM_I()->min_seq_blocks))	return true;
	return false;
}

//static int __f2fs_write_data_pages(address_space *mapping, writeback_control *wbc, enum iostat_type io_type)
int Cf2fsDataMapping::__f2fs_write_data_pages(writeback_control* wbc, iostat_type io_type)
{
	inode *inode = host;
	f2fs_inode_info* fi = F2FS_I(inode);
	f2fs_sb_info *sbi = fi->m_sbi;
	blk_plug plug;
	int ret;
	bool locked = false;

	/* deal with chardevs and other special file */
//	if (!this->a_ops->writepage)	return 0;
	//<YUAN>write_page()为虚函数，对于chardev和special file，虚函数应该实现空。
	// TODO：判断chardev或者special file，并且直接返回


	/* skip writing if there is no dirty page in this inode */
	if (!get_dirty_pages(inode) && wbc->sync_mode == WB_SYNC_NONE)	return 0;

	/* during POR, we don't need to trigger writepage at all. */ 
	// POR: Power On Recovery.在Mount的一开始设置，完成初始化后清除（f2fs_fill_super()函数中）
	if (unlikely(sbi->is_sbi_flag_set( SBI_POR_DOING)))
		goto skip_write;

	if ((S_ISDIR(inode->i_mode) || IS_NOQUOTA(inode)) && wbc->sync_mode == WB_SYNC_NONE &&
			get_dirty_pages(inode) < nr_pages_to_skip(sbi, DATA) &&	f2fs_available_free_memory(sbi, DIRTY_DENTS))
		goto skip_write;

	/* skip writing during file defragment */
	if (is_inode_flag_set(fi, FI_DO_DEFRAG))
		goto skip_write;

//	trace_f2fs_writepages(this->host, wbc, DATA);

	/* to avoid spliting IOs due to mixed WB_SYNC_ALL and WB_SYNC_NONE */
	if (wbc->sync_mode == WB_SYNC_ALL)					atomic_inc(&sbi->wb_sync_req[DATA]);
	else if (atomic_read(&sbi->wb_sync_req[DATA]))		goto skip_write;

	if (__should_serialize_io(fi, wbc)) 
	{
		mutex_lock(&sbi->writepages);
		locked = true;
	}

	blk_start_plug(&plug);
	ret = f2fs_write_cache_pages(wbc, io_type);
	blk_finish_plug(&plug);

	if (locked)
		mutex_unlock(&sbi->writepages);

	if (wbc->sync_mode == WB_SYNC_ALL)
		atomic_dec(&sbi->wb_sync_req[DATA]);
	/* if some pages were truncated, we cannot guarantee its mapping->host to detect pending bios. */

	f2fs_remove_dirty_inode(fi);
	return ret;

skip_write:
	wbc->pages_skipped += get_dirty_pages(inode);
//	trace_f2fs_writepages(mapping->host, wbc, DATA);
	return 0;
}

//static int f2fs_write_data_pages(address_space *mapping, struct writeback_control *wbc)
int Cf2fsDataMapping::write_pages(writeback_control * wbc)
{
	//<YUAN> 源代码通过是否是当前线程，判断是CP IO还是DATA IO. TODO: 此处要增加判断方法，目前只有CP的IO
	f2fs_inode_info* iinode = F2FS_I(host);
	DWORD cp_task_id = iinode->m_sbi->cprc_info.GetThreadId();
	DWORD cur_id = GetCurrentThreadId();
	iostat_type type = (cp_task_id == cur_id) ? FS_CP_DATA_IO : FS_DATA_IO;

	return __f2fs_write_data_pages(wbc, type);
}

static void f2fs_write_failed(address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;
	f2fs_inode_info* fi = F2FS_I(inode);
	loff_t i_size = i_size_read(inode);

	if (IS_NOQUOTA(inode))	return;

	/* In the fs-verity case, f2fs_end_enable_verity() does the truncate */
	if (to > i_size && !f2fs_verity_in_progress(fi)) 
	{
		down_write(&F2FS_I(inode)->i_gc_rwsem[WRITE]);
		down_write(&F2FS_I(inode)->i_mmap_sem);

		truncate_pagecache(inode, i_size);
		fi->f2fs_truncate_blocks(i_size, true);

		up_write(&F2FS_I(inode)->i_mmap_sem);
		up_write(&F2FS_I(inode)->i_gc_rwsem[WRITE]);
	}
}

static int prepare_write_begin(struct f2fs_sb_info *sbi, struct page *page, loff_t pos, unsigned len,
			block_t *blk_addr, bool *node_changed)
{
	f2fs_inode_info *inode = F2FS_I(page->mapping->host);

	pgoff_t index = page->index;
	struct dnode_of_data dn;
	struct page *ipage;
	bool locked = false;
	struct extent_info ei = {0,0,0};
	int err = 0;
	int flag;

	/* we already allocated all the blocks, so we don't need to get the block addresses when there is no need to fill
	the page. */
	if (!f2fs_has_inline_data(inode) && len == PAGE_SIZE && !is_inode_flag_set(inode, FI_NO_PREALLOC) && !f2fs_verity_in_progress(inode))
		return 0;

	/* f2fs_lock_op avoids race between write CP and convert_inline_page */
	if (f2fs_has_inline_data(inode) && pos + len > MAX_INLINE_DATA(inode))	flag = F2FS_GET_BLOCK_DEFAULT;
	else		flag = F2FS_GET_BLOCK_PRE_AIO;

	if (f2fs_has_inline_data(inode) || (pos & PAGE_MASK) >= i_size_read(inode)) {
		f2fs_do_map_lock(sbi, flag, true);
		locked = true;
	}

restart:
	/* check inline_data */
	ipage = sbi->f2fs_get_node_page( inode->i_ino);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto unlock_out;
	}

	dn.set_new_dnode(inode, ipage, ipage, 0);

	if (f2fs_has_inline_data(inode)) {
		if (pos + len <= MAX_INLINE_DATA(inode)) {
			f2fs_do_read_inline_data(page, ipage);
			set_inode_flag(inode, FI_DATA_EXIST);
			if (inode->i_nlink)					set_inline_node(ipage);
		} 
		else {
			err = f2fs_convert_inline_page(&dn, page);
			if (err)							goto out;
			if (dn.data_blkaddr == NULL_ADDR)	err = f2fs_get_block(&dn, index);
		}
	} 
	else if (locked) {
		err = f2fs_get_block(&dn, index);
	} 
	else {
		if (f2fs_lookup_extent_cache(inode, index, &ei)) {
			dn.data_blkaddr = ei.blk + index - ei.fofs;
		} 
		else {
			/* hole case */
			err = f2fs_get_dnode_of_data(&dn, index, LOOKUP_NODE);
			if (err || dn.data_blkaddr == NULL_ADDR) {
				f2fs_put_dnode(&dn);
				f2fs_do_map_lock(sbi, F2FS_GET_BLOCK_PRE_AIO, true);
				WARN_ON(flag != F2FS_GET_BLOCK_PRE_AIO);
				locked = true;
				goto restart;
			}
		}
	}

	/* convert_inline_page can make node_changed */
	*blk_addr = dn.data_blkaddr;
	*node_changed = dn.node_changed;
out:
	f2fs_put_dnode(&dn);
unlock_out:
	if (locked)
		f2fs_do_map_lock(sbi, flag, false);
	return err;
}

//static int f2fs_write_begin(struct file *file, address_space *mapping, loff_t pos, unsigned len, unsigned flags,
//		struct page **pagep, void **fsdata)
int Cf2fsDataMapping::write_begin(file* file, loff_t pos, unsigned len, unsigned flags, page** pagep, void** fsdata)
{
	f2fs_inode_info *inode = dynamic_cast<f2fs_inode_info*>(host);
	f2fs_sb_info *sbi = F2FS_I_SB(inode);
	page *ppage = NULL;
	pgoff_t index = ((unsigned long long) pos) >> PAGE_SHIFT;
	bool need_balance = false, drop_atomic = false;
	block_t blkaddr = NULL_ADDR;
	int err = 0;

//	trace_f2fs_write_begin(inode, pos, len, flags);

	if (!sbi->f2fs_is_checkpoint_ready()) 
	{
		LOG_ERROR(L"[err] chekpoint is not ready");
		err = -ENOSPC;
		goto fail;
	}

	if ((f2fs_is_atomic_file(inode) && !f2fs_available_free_memory(sbi, INMEM_PAGES)) ||
			is_inode_flag_set(inode, FI_ATOMIC_REVOKE_REQUEST)) 
	{
		err = -ENOMEM;
		drop_atomic = true;
		goto fail;
	}

	/* We should check this at this moment to avoid deadlock on inode page and #0 page. The locking rule for inline_data conversion should be: lock_page(page #0) -> lock_page(inode_page) */
	if (index != 0) 
	{
		err = inode->f2fs_convert_inline_inode();
		if (err)	goto fail;
	}

#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (f2fs_compressed_file(inode)) 
	{
		int ret;
		*fsdata = NULL;
		ret = f2fs_prepare_compress_overwrite(inode, pagep,	index, fsdata);
		if (ret < 0) {	err = ret;	goto fail;	} 
		else if (ret) {	return 0;	}
	}
#endif

repeat:
	/* Do not use grab_cache_page_write_begin() to avoid deadlock due to wait_for_stable_page. Will wait that below with our IO control.	 */
	ppage = f2fs_pagecache_get_page(this, index, FGP_LOCK | FGP_WRITE | FGP_CREAT, GFP_NOFS);
	if (!ppage)
	{
		err = -ENOMEM;
		goto fail;
	}

	/* TODO: cluster can be compressed due to race with .writepage */
	*pagep = ppage;

	err = prepare_write_begin(sbi, ppage, pos, len, &blkaddr, &need_balance);
	if (err) goto fail;

	if (need_balance && !IS_NOQUOTA(inode) && sbi->has_not_enough_free_secs(0, 0)) 
	{
		unlock_page(ppage);
		sbi->f2fs_balance_fs(true);
		lock_page(ppage);
		if (ppage->mapping != this) 
		{	/* The page got truncated from under us */
			f2fs_put_page(ppage, 1);
			goto repeat;
		}
	}

	f2fs_wait_on_page_writeback(ppage, DATA, false, true);

	if (len == PAGE_SIZE || PageUptodate(ppage))	return 0;

	if (!(pos & (PAGE_SIZE - 1)) && (pos + len) >= i_size_read(inode) && !f2fs_verity_in_progress(inode)) 
	{
		zero_user_segment(ppage, len, PAGE_SIZE);
		return 0;
	}

	if (blkaddr == NEW_ADDR) 
	{
		zero_user_segment(ppage, 0, PAGE_SIZE);
		SetPageUptodate(ppage);
	}
	else
	{
		if (!sbi->f2fs_is_valid_blkaddr(blkaddr, DATA_GENERIC_ENHANCE_READ)) 
		{
			err = -EFSCORRUPTED;
			goto fail;
		}
		err = sbi->f2fs_submit_page_read(inode, ppage, blkaddr, 0, true);
		if (err)	goto fail;

		lock_page(ppage);
		if (unlikely(ppage->mapping != this)) 
		{
			f2fs_put_page(ppage, 1);
			goto repeat;
		}
		if (unlikely(!PageUptodate(ppage))) 
		{
			err = -EIO;
			goto fail;
		}
	}
	return 0;

fail:
	f2fs_put_page(ppage, 1);
	f2fs_write_failed(this, pos + len);
	if (drop_atomic)	f2fs_drop_inmem_pages_all(sbi, false);
	return err;
}

//static int f2fs_write_end(struct file *file,	address_space *mapping,
//			loff_t pos, unsigned len, unsigned copied,	struct page *page, void *fsdata)
int Cf2fsDataMapping::write_end(file* file, loff_t pos, unsigned len, unsigned copied, page* page, void* fsdata)
{
	f2fs_inode_info *inode = F2FS_I(page->mapping->host);
//	trace_f2fs_write_end(inode, pos, len, copied);
	/* This should be come from len == PAGE_SIZE, and we expect copied should be PAGE_SIZE. Otherwise, we treat it with zero copied and let generic_perform_write() try to copy data again through copied=0.	 */
	if (!PageUptodate(page)) 
	{
		if (unlikely(copied != len))			copied = 0;
		else			SetPageUptodate(page);
	}
#ifdef CONFIG_F2FS_FS_COMPRESSION
	/* overwrite compressed file */
	if (f2fs_compressed_file(inode) && fsdata) {
		f2fs_compress_write_end(inode, fsdata, page->index, copied);
		f2fs_update_time(F2FS_I_SB(inode), REQ_TIME);

		if (pos + copied > i_size_read(inode) &&
				!f2fs_verity_in_progress(inode))
			inode->f2fs_i_size_write(pos + copied);
		return copied;
	}
#endif
	if (!copied)		goto unlock_out;
	set_page_dirty(page);
	if (pos + copied > i_size_read(inode) &&    !f2fs_verity_in_progress(inode))
		inode->f2fs_i_size_write(pos + copied);
unlock_out:
	f2fs_put_page(page, 1);
	inode->m_sbi->f2fs_update_time(REQ_TIME);
	return copied;
}

#if 0

static int check_direct_IO(struct inode *inode, struct iov_iter *iter,
			   loff_t offset)
{
	unsigned i_blkbits = READ_ONCE(inode->i_blkbits);
	unsigned blkbits = i_blkbits;
	unsigned blocksize_mask = (1 << blkbits) - 1;
	unsigned long align = offset | iov_iter_alignment(iter);
	block_device *bdev = inode->i_sb->s_bdev;

	if (iov_iter_rw(iter) == READ && offset >= i_size_read(inode))
		return 1;

	if (align & blocksize_mask) {
		if (bdev)
			blkbits = blksize_bits(bdev_logical_block_size(bdev));
		blocksize_mask = (1 << blkbits) - 1;
		if (align & blocksize_mask)
			return -EINVAL;
		return 1;
	}
	return 0;
}

static void f2fs_dio_end_io(struct bio *bio)
{
	struct f2fs_private_dio *dio = bio->bi_private;

	dec_page_count(F2FS_I_SB(dio->inode),
			dio->write ? F2FS_DIO_WRITE : F2FS_DIO_READ);

	bio->bi_private = dio->orig_private;
	bio->bi_end_io = dio->orig_end_io;

	kfree(dio);

	bio_endio(bio);
}

static void f2fs_dio_submit_bio(struct bio *bio, struct inode *inode,
							loff_t file_offset)
{
	struct f2fs_private_dio *dio;
	bool write = (bio_op(bio) == REQ_OP_WRITE);

	dio = f2fs_kzalloc(F2FS_I_SB(inode),
			sizeof(struct f2fs_private_dio), GFP_NOFS);
	if (!dio)
		goto out;

	dio->inode = inode;
	dio->orig_end_io = bio->bi_end_io;
	dio->orig_private = bio->bi_private;
	dio->write = write;

	bio->bi_end_io = f2fs_dio_end_io;
	bio->bi_private = dio;

	inc_page_count(F2FS_I_SB(inode),
			write ? F2FS_DIO_WRITE : F2FS_DIO_READ);

	submit_bio(bio);
	return;
out:
	bio->bi_status = BLK_STS_IOERR;
	bio_endio(bio);
}
#endif

//static ssize_t f2fs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
ssize_t Cf2fsDataMapping::direct_IO(kiocb* iocb, iov_iter * iter)
{
#if 0
	address_space *mapping = iocb->ki_filp->f_mapping;
	struct inode *inode = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	size_t count = iov_iter_count(iter);
	loff_t offset = iocb->ki_pos;
	int rw = iov_iter_rw(iter);
	int err;
	enum rw_hint hint = iocb->ki_hint;
	int whint_mode = F2FS_OPTION(sbi).whint_mode;
	bool do_opu;

	err = check_direct_IO(inode, iter, offset);
	if (err)
		return err < 0 ? err : 0;

	if (f2fs_force_buffered_io(inode, iocb, iter))
		return 0;

	do_opu = allow_outplace_dio(inode, iocb, iter);

	trace_f2fs_direct_IO_enter(inode, offset, count, rw);

	if (rw == WRITE && whint_mode == WHINT_MODE_OFF)
		iocb->ki_hint = WRITE_LIFE_NOT_SET;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!down_read_trylock(&fi->i_gc_rwsem[rw])) {
			iocb->ki_hint = hint;
			err = -EAGAIN;
			goto out;
		}
		if (do_opu && !down_read_trylock(&fi->i_gc_rwsem[READ])) {
			up_read(&fi->i_gc_rwsem[rw]);
			iocb->ki_hint = hint;
			err = -EAGAIN;
			goto out;
		}
	} else {
		down_read(&fi->i_gc_rwsem[rw]);
		if (do_opu)
			down_read(&fi->i_gc_rwsem[READ]);
	}

	err = __blockdev_direct_IO(iocb, inode, inode->i_sb->s_bdev,
			iter, rw == WRITE ? get_data_block_dio_write :
			get_data_block_dio, NULL, f2fs_dio_submit_bio,
			rw == WRITE ? DIO_LOCKING | DIO_SKIP_HOLES :
			DIO_SKIP_HOLES);

	if (do_opu)
		up_read(&fi->i_gc_rwsem[READ]);

	up_read(&fi->i_gc_rwsem[rw]);

	if (rw == WRITE) {
		if (whint_mode == WHINT_MODE_OFF)
			iocb->ki_hint = hint;
		if (err > 0) {
			f2fs_update_iostat(F2FS_I_SB(inode), APP_DIRECT_IO,
									err);
			if (!do_opu)
				set_inode_flag(inode, FI_UPDATE_WRITE);
		} else if (err == -EIOCBQUEUED) {
			f2fs_update_iostat(F2FS_I_SB(inode), APP_DIRECT_IO,
						count - iov_iter_count(iter));
		} else if (err < 0) {
			f2fs_write_failed(mapping, offset + count);
		}
	} else {
		if (err > 0)
			sbi->f2fs_update_iostat(APP_DIRECT_READ_IO, err);
		else if (err == -EIOCBQUEUED)
			f2fs_update_iostat(F2FS_I_SB(inode), APP_DIRECT_READ_IO,
						count - iov_iter_count(iter));
	}

out:
	trace_f2fs_direct_IO_exit(inode, offset, count, rw, err);

	return err;
#else
	JCASSERT(0);
	return -1;
#endif
}

Cf2fsMappingBase::Cf2fsMappingBase(f2fs_inode_info* iinode, CPageManager * manager) : address_space(manager)
{
	JCASSERT(iinode);
	INIT_LIST_HEAD(&private_list);
	host = static_cast<inode*>(iinode);
}

//void f2fs_invalidate_page(struct page *page, unsigned int offset, unsigned int length)
void Cf2fsMappingBase::invalidate_page(page* ppage, unsigned int offset, unsigned int length)
{
	JCASSERT(ppage->mapping == this)
	f2fs_inode_info *iinode = F2FS_I(host);
	f2fs_sb_info* sbi = iinode->m_sbi;

	if (iinode->i_ino >= sbi->F2FS_ROOT_INO() &&	(offset % PAGE_SIZE || length != PAGE_SIZE))
		return;

	if (PageDirty(ppage)) 
	{
#ifdef DEBUG_PAGE
		LOG_TRACK(L"page.dirty", L" dec: inode=%d, page=%d, flag=%X, lock_th=%04X", iinode->i_ino, ppage->index, ppage->flags, ppage->lock_th_id);
#endif
//		ClearPageDirty(ppage);
		inode_dec_dirty_pages(iinode);
		f2fs_remove_dirty_inode(iinode);
	}

	clear_cold_data(ppage);

	if (IS_ATOMIC_WRITTEN_PAGE(ppage))		return f2fs_drop_inmem_page(iinode, ppage);
	TRACK_PAGE(ppage, L"clear private");
	f2fs_clear_page_private(ppage);
}

void Cf2fsNodeMapping::invalidate_page(page* ppage, unsigned int offset, unsigned int length)
{
	JCASSERT(ppage->mapping == this);
	f2fs_inode_info* iinode = F2FS_I(host);
	f2fs_sb_info* sbi = iinode->m_sbi;
	JCASSERT(iinode->i_ino == sbi->F2FS_NODE_INO());
	if (PageDirty(ppage))	sbi->dec_page_count(F2FS_DIRTY_NODES);
	clear_cold_data(ppage);
	if (IS_ATOMIC_WRITTEN_PAGE(ppage))		return f2fs_drop_inmem_page(iinode, ppage);
	TRACK_PAGE(ppage, L"clear private");
	f2fs_clear_page_private(ppage);

}

void Cf2fsMetaMapping::invalidate_page(page* ppage, unsigned int offset, unsigned int length)
{
	JCASSERT(ppage->mapping == this);
	f2fs_inode_info* iinode = F2FS_I(host);
	f2fs_sb_info* sbi = iinode->m_sbi;
	JCASSERT(iinode->i_ino == sbi->F2FS_META_INO());
	if (PageDirty(ppage))				sbi->dec_page_count(F2FS_DIRTY_META);
	clear_cold_data(ppage);
	if (IS_ATOMIC_WRITTEN_PAGE(ppage))		return f2fs_drop_inmem_page(iinode, ppage);
	TRACK_PAGE(ppage, L"clear private");
	f2fs_clear_page_private(ppage);
}



//int f2fs_release_page(struct page *page, gfp_t wait)
int Cf2fsMappingBase::release_page(page* page, gfp_t wait)
{
	/* If this is dirty page, keep PagePrivate */
	if (PageDirty(page))	return 0;
	/* This is atomic written page, keep Private */
	if (IS_ATOMIC_WRITTEN_PAGE(page))	return 0;
	clear_cold_data(page);
	f2fs_clear_page_private(page);
	return 1;

}


//static int f2fs_set_data_page_dirty(struct page *page)
int Cf2fsDataMapping::set_node_page_dirty(page * page)
{

	struct inode* inode = host; //page_file_mapping(page)->host;
	f2fs_inode_info* fi = F2FS_I(inode);
//	trace_f2fs_set_page_dirty(page, DATA);

	if (!PageUptodate(page))		SetPageUptodate(page);
	if (PageSwapCache(page))		return __set_page_dirty_nobuffers(page);

	if (f2fs_is_atomic_file(fi) && !f2fs_is_commit_atomic_write(fi))
	{
		if (!IS_ATOMIC_WRITTEN_PAGE(page))
		{
			f2fs_register_inmem_page(inode, page);
			return 1;
		}
		/* Previously, this page has been registered, we just return here.	 */
		return 0;
	}

	if (!PageDirty(page))
	{
		__set_page_dirty_nobuffers(page);
		LOG_TRACK(L"page.dirty", L" update dirty, inode=%d, page=%d", fi->i_ino, page->index);
		f2fs_update_dirty_page(fi, page);
		return 1;
	}
	return 0;
}

#if 0

static sector_t f2fs_bmap_compress(struct inode *inode, sector_t block)
{
#ifdef CONFIG_F2FS_FS_COMPRESSION
	struct dnode_of_data dn;
	sector_t start_idx, blknr = 0;
	int ret;

	start_idx = round_down(block, F2FS_I(inode)->i_cluster_size);

	dn.set_new_dnode(inode, NULL, NULL, 0);
	ret = f2fs_get_dnode_of_data(&dn, start_idx, LOOKUP_NODE);
	if (ret)
		return 0;

	if (dn.data_blkaddr != COMPRESS_ADDR) {
		dn.ofs_in_node += block - start_idx;
		blknr = f2fs_data_blkaddr(&dn);
		if (!__is_valid_data_blkaddr(blknr))
			blknr = 0;
	}

	f2fs_put_dnode(&dn);
	return blknr;
#else
	return 0;
#endif
}

#endif

//static sector_t f2fs_bmap(address_space *mapping, sector_t block)
sector_t Cf2fsDataMapping::bmap(sector_t block)
{

	struct inode *inode = host;
#if 0
	sector_t blknr = 0;

	if (f2fs_has_inline_data(inode))
		goto out;

	/* make sure allocating whole blocks */
	if (mapping_tagged(mapping, PAGECACHE_TAG_DIRTY))
		filemap_write_and_wait(mapping);

	/* Block number less than F2FS MAX BLOCKS */
	if (unlikely(block >= max_file_blocks(inode)))
		goto out;

	if (f2fs_compressed_file(inode)) {
		blknr = f2fs_bmap_compress(inode, block);
	} else {
		struct f2fs_map_blocks map;

		memset(&map, 0, sizeof(map));
		map.m_lblk = block;
		map.m_len = 1;
		map.m_next_pgofs = NULL;
		map.m_seg_type = NO_CHECK_TYPE;

		if (!f2fs_map_blocks(inode, &map, 0, F2FS_GET_BLOCK_BMAP))
			blknr = map.m_pblk;
	}
out:
	trace_f2fs_bmap(inode, block, blknr);
	return blknr;
#else
	JCASSERT(0);
	return -1;
#endif
}

#if 0

#ifdef CONFIG_MIGRATION
#include <linux/migrate.h>

int f2fs_migrate_page(address_space *mapping,
		struct page *newpage, struct page *page, enum migrate_mode mode)
{
	int rc, extra_count;
	struct f2fs_inode_info *fi = F2FS_I(mapping->host);
	bool atomic_written = IS_ATOMIC_WRITTEN_PAGE(page);

	BUG_ON(PageWriteback(page));

	/* migrating an atomic written page is safe with the inmem_lock hold */
	if (atomic_written) {
		if (mode != MIGRATE_SYNC)
			return -EBUSY;
		if (!mutex_trylock(&fi->inmem_lock))
			return -EAGAIN;
	}

	/* one extra reference was held for atomic_write page */
	extra_count = atomic_written ? 1 : 0;
	rc = migrate_page_move_mapping(mapping, newpage,
				page, extra_count);
	if (rc != MIGRATEPAGE_SUCCESS) {
		if (atomic_written)
			mutex_unlock(&fi->inmem_lock);
		return rc;
	}

	if (atomic_written) {
		struct inmem_pages *cur;

		list_for_each_entry(cur, &fi->inmem_pages, list)
			if (cur->page == page) {
				cur->page = newpage;
				break;
			}
		mutex_unlock(&fi->inmem_lock);
		page->put_page();
		get_page(newpage);
	}

	if (PagePrivate(page)) {
		f2fs_set_page_private(newpage, page_private(page));
		f2fs_clear_page_private(page);
	}

	if (mode != MIGRATE_SYNC_NO_COPY)
		migrate_page_copy(newpage, page);
	else
		migrate_page_states(newpage, page);

	return MIGRATEPAGE_SUCCESS;
}
#endif

#endif	//TODO

#ifdef CONFIG_SWAP
static int f2fs_is_file_aligned(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	block_t main_blkaddr = sbi->SM_I()->main_blkaddr;
	block_t cur_lblock;
	block_t last_lblock;
	block_t pblock;
	unsigned long nr_pblocks;
	unsigned int blocks_per_sec = BLKS_PER_SEC(sbi);
	unsigned int not_aligned = 0;
	int ret = 0;

	cur_lblock = 0;
	last_lblock = bytes_to_blks(inode, i_size_read(inode));

	while (cur_lblock < last_lblock) {
		struct f2fs_map_blocks map;

		memset(&map, 0, sizeof(map));
		map.m_lblk = cur_lblock;
		map.m_len = last_lblock - cur_lblock;
		map.m_next_pgofs = NULL;
		map.m_next_extent = NULL;
		map.m_seg_type = NO_CHECK_TYPE;
		map.m_may_create = false;

		ret = f2fs_map_blocks(inode, &map, 0, F2FS_GET_BLOCK_FIEMAP);
		if (ret)
			goto out;

		/* hole */
		if (!(map.m_flags & F2FS_MAP_FLAGS)) {
			f2fs_err(sbi, "Swapfile has holes\n");
			ret = -ENOENT;
			goto out;
		}

		pblock = map.m_pblk;
		nr_pblocks = map.m_len;

		if ((pblock - main_blkaddr) & (blocks_per_sec - 1) ||
			nr_pblocks & (blocks_per_sec - 1)) {
			if (f2fs_is_pinned_file(inode)) {
				f2fs_err(sbi, "Swapfile does not align to section");
				ret = -EINVAL;
				goto out;
			}
			not_aligned++;
		}

		cur_lblock += nr_pblocks;
	}
	if (not_aligned)
		f2fs_warn(sbi, "Swapfile (%u) is not align to section: \n"
			"\t1) creat(), 2) ioctl(F2FS_IOC_SET_PIN_FILE), 3) fallocate()",
			not_aligned);
out:
	return ret;
}

static int check_swap_activate_fast(struct swap_info_struct *sis,
				struct file *swap_file, sector_t *span)
{
	address_space *mapping = swap_file->f_mapping;
	struct inode *inode = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	sector_t cur_lblock;
	sector_t last_lblock;
	sector_t pblock;
	sector_t lowest_pblock = -1;
	sector_t highest_pblock = 0;
	int nr_extents = 0;
	unsigned long nr_pblocks;
	unsigned int blocks_per_sec = BLKS_PER_SEC(sbi);
	unsigned int not_aligned = 0;
	int ret = 0;

	/*
	 * Map all the blocks into the extent list.  This code doesn't try
	 * to be very smart.
	 */
	cur_lblock = 0;
	last_lblock = bytes_to_blks(inode, i_size_read(inode));

	while (cur_lblock < last_lblock && cur_lblock < sis->max) {
		struct f2fs_map_blocks map;

		cond_resched();

		memset(&map, 0, sizeof(map));
		map.m_lblk = cur_lblock;
		map.m_len = last_lblock - cur_lblock;
		map.m_next_pgofs = NULL;
		map.m_next_extent = NULL;
		map.m_seg_type = NO_CHECK_TYPE;
		map.m_may_create = false;

		ret = f2fs_map_blocks(inode, &map, 0, F2FS_GET_BLOCK_FIEMAP);
		if (ret)
			goto out;

		/* hole */
		if (!(map.m_flags & F2FS_MAP_FLAGS)) {
			f2fs_err(sbi, "Swapfile has holes\n");
			ret = -EINVAL;
			goto out;
		}

		pblock = map.m_pblk;
		nr_pblocks = map.m_len;

		if ((pblock - sbi->SM_I()->main_blkaddr) & (blocks_per_sec - 1) ||
				nr_pblocks & (blocks_per_sec - 1)) {
			if (f2fs_is_pinned_file(inode)) {
				f2fs_err(sbi, "Swapfile does not align to section");
				ret = -EINVAL;
				goto out;
			}
			not_aligned++;
		}

		if (cur_lblock + nr_pblocks >= sis->max)
			nr_pblocks = sis->max - cur_lblock;

		if (cur_lblock) {	/* exclude the header page */
			if (pblock < lowest_pblock)
				lowest_pblock = pblock;
			if (pblock + nr_pblocks - 1 > highest_pblock)
				highest_pblock = pblock + nr_pblocks - 1;
		}

		/*
		 * We found a PAGE_SIZE-length, PAGE_SIZE-aligned run of blocks
		 */
		ret = add_swap_extent(sis, cur_lblock, nr_pblocks, pblock);
		if (ret < 0)
			goto out;
		nr_extents += ret;
		cur_lblock += nr_pblocks;
	}
	ret = nr_extents;
	*span = 1 + highest_pblock - lowest_pblock;
	if (cur_lblock == 0)
		cur_lblock = 1;	/* force Empty message */
	sis->max = cur_lblock;
	sis->pages = cur_lblock - 1;
	sis->highest_bit = cur_lblock - 1;

	if (not_aligned)
		f2fs_warn(sbi, "Swapfile (%u) is not align to section: \n"
			"\t1) creat(), 2) ioctl(F2FS_IOC_SET_PIN_FILE), 3) fallocate()",
			not_aligned);
out:
	return ret;
}

/* Copied from generic_swapfile_activate() to check any holes */
static int check_swap_activate(struct swap_info_struct *sis,
				struct file *swap_file, sector_t *span)
{
	address_space *mapping = swap_file->f_mapping;
	struct inode *inode = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	unsigned blocks_per_page;
	unsigned long page_no;
	sector_t probe_block;
	sector_t last_block;
	sector_t lowest_block = -1;
	sector_t highest_block = 0;
	int nr_extents = 0;
	int ret = 0;

	if (PAGE_SIZE == F2FS_BLKSIZE)
		return check_swap_activate_fast(sis, swap_file, span);

	ret = f2fs_is_file_aligned(inode);
	if (ret)
		goto out;

	blocks_per_page = bytes_to_blks(inode, PAGE_SIZE);

	/*
	 * Map all the blocks into the extent list.  This code doesn't try
	 * to be very smart.
	 */
	probe_block = 0;
	page_no = 0;
	last_block = bytes_to_blks(inode, i_size_read(inode));
	while ((probe_block + blocks_per_page) <= last_block &&
			page_no < sis->max) {
		unsigned block_in_page;
		sector_t first_block;
		sector_t block = 0;

		cond_resched();

		block = probe_block;
		ret = bmap(inode, &block);
		if (ret)
			goto out;
		if (!block)
			goto bad_bmap;
		first_block = block;

		/*
		 * It must be PAGE_SIZE aligned on-disk
		 */
		if (first_block & (blocks_per_page - 1)) {
			probe_block++;
			goto reprobe;
		}

		for (block_in_page = 1; block_in_page < blocks_per_page;
					block_in_page++) {

			block = probe_block + block_in_page;
			ret = bmap(inode, &block);
			if (ret)
				goto out;
			if (!block)
				goto bad_bmap;

			if (block != first_block + block_in_page) {
				/* Discontiguity */
				probe_block++;
				goto reprobe;
			}
		}

		first_block >>= (PAGE_SHIFT - inode->i_blkbits);
		if (page_no) {	/* exclude the header page */
			if (first_block < lowest_block)
				lowest_block = first_block;
			if (first_block > highest_block)
				highest_block = first_block;
		}

		/*
		 * We found a PAGE_SIZE-length, PAGE_SIZE-aligned run of blocks
		 */
		ret = add_swap_extent(sis, page_no, 1, first_block);
		if (ret < 0)
			goto out;
		nr_extents += ret;
		page_no++;
		probe_block += blocks_per_page;
reprobe:
		continue;
	}
	ret = nr_extents;
	*span = 1 + highest_block - lowest_block;
	if (page_no == 0)
		page_no = 1;	/* force Empty message */
	sis->max = page_no;
	sis->pages = page_no - 1;
	sis->highest_bit = page_no - 1;
out:
	return ret;
bad_bmap:
	f2fs_err(sbi, "Swapfile has holes\n");
	return -EINVAL;
}

static int f2fs_swap_activate(struct swap_info_struct *sis, struct file *file, sector_t *span)
{
	struct inode *inode = file_inode(file);
	int ret;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	if (f2fs_readonly(F2FS_I_SB(inode)->sb))
		return -EROFS;

	if (f2fs_lfs_mode(F2FS_I_SB(inode))) {
		f2fs_err(F2FS_I_SB(inode),
			"Swapfile not supported in LFS mode");
		return -EINVAL;
	}

	ret = inode->f2fs_convert_inline_inode();
	if (ret)
		return ret;

	if (!f2fs_disable_compressed_file(inode))
		return -EINVAL;

	f2fs_precache_extents(inode);

	ret = check_swap_activate(sis, file, span);
	if (ret < 0)
		return ret;

	set_inode_flag(inode, FI_PIN_FILE);
	f2fs_update_time(F2FS_I_SB(inode), REQ_TIME);
	return ret;
}

static void f2fs_swap_deactivate(struct file *file)
{
	struct inode *inode = file_inode(file);

	clear_inode_flag(inode, FI_PIN_FILE);
}
#else
//static int f2fs_swap_activate(struct swap_info_struct *sis, struct file *file, sector_t *span)
int Cf2fsDataMapping::swap_activate(swap_info_struct * sis, file * file, sector_t *span)
{
	return -EOPNOTSUPP;
}

//static void f2fs_swap_deactivate(struct file *file)
void Cf2fsDataMapping::swap_deactivate(file * file)
{
}
#endif

//
//const struct address_space_operations f2fs_dblock_aops = {
//	.readpage	= f2fs_read_data_page,
//	.readahead	= f2fs_readahead,
//	.writepage	= f2fs_write_data_page,
//	.writepages	= f2fs_write_data_pages,
//	.write_begin	= f2fs_write_begin,
//	.write_end	= f2fs_write_end,
//	.set_page_dirty	= f2fs_set_data_page_dirty,
//	.invalidatepage	= f2fs_invalidate_page,
//	.releasepage	= f2fs_release_page,
//	.direct_IO	= f2fs_direct_IO,
//	.bmap		= f2fs_bmap,
//	.swap_activate  = f2fs_swap_activate,
//	.swap_deactivate = f2fs_swap_deactivate,
//#ifdef CONFIG_MIGRATION
//	.migratepage    = f2fs_migrate_page,
//#endif
//};


void f2fs_clear_page_cache_dirty_tag(struct page *page)
{
	address_space *mapping = page_mapping(page);
//	unsigned long flags;

#if 0 // TODO
	xa_lock_irqsave(&mapping->i_pages, flags);
#endif
	__xa_clear_mark(&mapping->i_pages, page_index(page), PAGECACHE_TAG_DIRTY);
#if 0 // TODO
	xa_unlock_irqrestore(&mapping->i_pages, flags);
#endif
}

#if 0 //TODO

int __init f2fs_init_post_read_processing(void)
{
	bio_post_read_ctx_cache =
		kmem_cache_create("f2fs_bio_post_read_ctx",
				  sizeof(struct bio_post_read_ctx), 0, 0, NULL);
	if (!bio_post_read_ctx_cache)
		goto fail;
	bio_post_read_ctx_pool =
		mempool_create_slab_pool(NUM_PREALLOC_POST_READ_CTXS,
					 bio_post_read_ctx_cache);
	if (!bio_post_read_ctx_pool)
		goto fail_free_cache;
	return 0;

fail_free_cache:
	kmem_cache_destroy(bio_post_read_ctx_cache);
fail:
	return -ENOMEM;
}

void f2fs_destroy_post_read_processing(void)
{
	mempool_destroy(bio_post_read_ctx_pool);
	kmem_cache_destroy(bio_post_read_ctx_cache);
}

#endif
int f2fs_init_post_read_wq(struct f2fs_sb_info *sbi)
{
	if (!f2fs_sb_has_encrypt(sbi) && !f2fs_sb_has_verity(sbi) && !f2fs_sb_has_compression(sbi))
		return 0;
	JCASSERT(0);
#if 0	//TODO
	// read workqueue，用于并行处理
	sbi->post_read_wq = alloc_workqueue("f2fs_post_read_wq", WQ_UNBOUND | WQ_HIGHPRI, num_online_cpus());
	if (!sbi->post_read_wq)	return -ENOMEM;
#endif
	return 0;
}


void f2fs_destroy_post_read_wq(struct f2fs_sb_info *sbi)
{
	//if (sbi->post_read_wq)
	//	destroy_workqueue(sbi->post_read_wq);
}

#if 0

int __init f2fs_init_bio_entry_cache(void)
{
	bio_entry_slab = f2fs_kmem_cache_create("f2fs_bio_entry_slab", sizeof(struct bio_entry));
	if (!bio_entry_slab)
		return -ENOMEM;
	return 0;
}

void f2fs_destroy_bio_entry_cache(void)
{
	kmem_cache_destroy(bio_entry_slab);
}

#endif //TODO

//static void submit_bio_wait_endio(bio* bb)
//{
////	complete(bio->bi_private);
//	HANDLE wait_event = (HANDLE)(bb->bi_private);
//	SetEvent(wait_event);
//}

int f2fs_sb_info::submit_bio_wait(bio* bbio)
{
	bbio->bi_end_io = nullptr;
	bbio->bi_opf |= REQ_SYNC;
	submit_sync_io(bbio);
	return blk_status_to_errno(bbio->bi_status);
}

//void f2fs_sb_info::submit_bio(bio* bb)
void f2fs_sb_info::submit_sync_io(bio* bb)
{
//	JCASSERT(bb && bb->bi_io_vec && bb->bi_bdev);	// bb->bi_io_vec可以为空
	JCASSERT(bb && bb->bi_bdev);
	// 模拟处理block io，暂时做sync read/write
	//	BIO:从bio.bi_iter.bi_sector开始，读取bi_size大小的数据。存入一组page中，page的地址是不连续的。
	//	这对于底层没有问题，可以使用页面映射。对于NVMe类存储器，可以直接回写不同page的数据。
	//	暂时通过复制实现。(1)下一步优化时，在VirtualDisk中实现BIO读写。(2)支持后台读写
	//	结果在bio->bi_status中反应，0：OK, !=0：error
	size_t data_size = bb->bi_iter.bi_size;
	size_t data_secs = BYTE_TO_SECTOR(data_size);
	jcvos::auto_array<BYTE> _buf(data_size);
	BYTE* buf = (BYTE*)_buf;		// 用于减少类型转换

	int op = bb->bi_opf & 0xFF;
//	LOG_TRACK(L"io",L"op=%s, sec=0x%llX, len=0x%llX", DebugOutReq(op), bb->bi_iter.bi_sector, data_secs);
	TRACK_BIO_IO(bb, L"start_io sync");

	if (op == REQ_OP_READ)
	{
		this->s_active;
		bool br = bb->bi_bdev->ReadSectors(buf, bb->bi_iter.bi_sector, data_secs);
		if (!br) THROW_ERROR(ERR_APP, L"failed on reading data from device, lba=0x%llX, secs=%lld", bb->bi_iter.bi_sector, data_secs);
		//<优化>能否使得page地址连续，这样可以避免一次内存复制。
		size_t buf_offset = 0;
		for (WORD ii = 0; ii < bb->bi_vcnt; ++ii)
		{
			bio_vec& vv = bb->bi_io_vec[ii];
			page* pp = vv.bv_page;
			JCASSERT(vv.bv_offset==0 && vv.bv_len == PAGE_CACHE_SIZE);
			memcpy_s(pp->virtual_add, vv.bv_len, buf + buf_offset, vv.bv_len);
			buf_offset += vv.bv_len;
#ifdef INODE_DEBUG
			f2fs_inode_info* inode = F2FS_I(pp->mapping->host);
			LOG_DEBUG_(1,L"read  page, ino=0x%X, type=%s, inode=%s, page=%p, for: %s", pp->index, pp->m_type.c_str(), 
				inode?inode->m_description.c_str(): L"null", pp, pp->m_description.c_str());
#endif
		}	
	}
	else if (op == REQ_OP_WRITE)
	{
		this->s_active;
		//<优化>能否使得page地址连续，这样可以避免一次内存复制。
		for (WORD ii = 0; ii < bb->bi_vcnt; ++ii)
		{
			bio_vec& vv = bb->bi_io_vec[ii];
			page* pp = vv.bv_page;
			JCASSERT(vv.bv_offset==0 && vv.bv_len == PAGE_CACHE_SIZE);
			memcpy_s(buf, vv.bv_len, pp->virtual_add, vv.bv_len);
			buf += vv.bv_len;
#ifdef INODE_DEBUG
			f2fs_inode_info* inode = F2FS_I(pp->mapping->host);
			LOG_DEBUG_(1,L"write page, ino=0x%X, type=%s, inode=%s, page=%p, for: %s", pp->index, pp->m_type.c_str(), 
				inode?inode->m_description.c_str(): L"null", pp, pp->m_description.c_str());
#endif
		}
		bool br = bb->bi_bdev->WriteSectors(_buf, bb->bi_iter.bi_sector, data_secs);
		if (!br) THROW_ERROR(ERR_APP, L"failed on write data from device, lba=0x%llX, secs=%lld", bb->bi_iter.bi_sector, data_secs);
		m_fs->UpdateDiskWrite(data_secs / 8);
		
//		f2fs_write_end_io(bb);
	}
	else
	{
		JCASSERT(0);
	}
	if (bb->bi_end_io) (bb->bi_end_io)(bb);
	TRACK_BIO_IO(bb, L"complete_io sync");

}

#if 0 // MOCE to io-complete-ctrl
void f2fs_sb_info::submit_async_io(bio* bb)
{
	//	JCASSERT(bb && bb->bi_io_vec && bb->bi_bdev);	// bb->bi_io_vec可以为空
	JCASSERT(bb && bb->bi_bdev);
	// 模拟处理block io，暂时做sync read/write
	//	BIO:从bio.bi_iter.bi_sector开始，读取bi_size大小的数据。存入一组page中，page的地址是不连续的。
	//	这对于底层没有问题，可以使用页面映射。对于NVMe类存储器，可以直接回写不同page的数据。
	//	暂时通过复制实现。(1)下一步优化时，在VirtualDisk中实现BIO读写。(2)支持后台读写
	//	结果在bio->bi_status中反应，0：OK, !=0：error
	size_t data_size = bb->bi_iter.bi_size;
	size_t data_secs = BYTE_TO_SECTOR(data_size);
	//jcvos::auto_array<BYTE> _buf(data_size);
	//BYTE* buf = (BYTE*)_buf;		// 用于减少类型转换

	bb->m_buf = new BYTE[data_size];
	if (bb->m_buf == nullptr) THROW_ERROR(ERR_MEM, L"failed allocate io buffer, size=%lld", data_size);

	int op = bb->bi_opf & 0xFF;
//	LOG_TRACK(L"io", L"op=%s, sec=0x%llX, len=0x%llX", DebugOutReq(op), bb->bi_iter.bi_sector, data_secs);
	TRACK_BIO_IO(bb, L"start_io async");
	memset(&bb->m_overlapped, 0, sizeof(OVERLAPPED));
	if (op == REQ_OP_READ)
	{
		bb->m_overlapped.hEvent = (HANDLE)(bb);
		size_t offset = SECTOR_TO_BYTE(bb->bi_iter.bi_sector);
		bb->m_overlapped.Offset = LODWORD(offset);
		bb->m_overlapped.OffsetHigh = HIDWORD(offset);
		bool br = bb->bi_bdev->AsyncReadSectors(bb->m_buf, data_secs, &bb->m_overlapped, ReadCompletionRoutine);
		if (!br) THROW_ERROR(ERR_APP, L"failed on reading data from device, lba=0x%llX, secs=%lld", bb->bi_iter.bi_sector, data_secs);
		//DWORD err = GetLastError();
	}
	else if (op == REQ_OP_WRITE)
	{
		BYTE* buf = bb->m_buf;

		for (WORD ii = 0; ii < bb->bi_vcnt; ++ii)
		{
			bio_vec& vv = bb->bi_io_vec[ii];
			page* pp = vv.bv_page;
			JCASSERT(vv.bv_offset == 0 && vv.bv_len == PAGE_CACHE_SIZE);
			memcpy_s(buf, vv.bv_len, pp->virtual_add, vv.bv_len);
			buf += vv.bv_len;
		}
		bb->m_overlapped.hEvent = (HANDLE)(bb);
		size_t offset = SECTOR_TO_BYTE(bb->bi_iter.bi_sector);
		bb->m_overlapped.Offset = LODWORD(offset);
		bb->m_overlapped.OffsetHigh = HIDWORD(offset);
		bool br = bb->bi_bdev->AsyncWriteSectors(bb->m_buf, data_secs, &bb->m_overlapped, WriteCompletionRoutine);

		//bool br = bb->bi_bdev->WriteSectors(_buf, bb->bi_iter.bi_sector, data_secs);
		if (!br) THROW_ERROR(ERR_APP, L"failed on write data from device, lba=0x%llX, secs=%lld", bb->bi_iter.bi_sector, data_secs);
		m_fs->UpdateDiskWrite(data_secs / 8);
	}
	else
	{
		// for debug
		THROW_ERROR(ERR_APP, L"unknown op code=%d", op);
	}
}
#endif


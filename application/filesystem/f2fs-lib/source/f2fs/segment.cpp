///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"

// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/segment.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include "../../include/f2fs.h"
#include "../../include/f2fs_fs.h"
#include "../../include/f2fs-filesystem.h"
//#include <linux/fs.h>
//#include <linux/f2fs_fs.h>
//#include <linux/bio.h>
//#include <linux/blkdev.h>
//#include <linux/prefetch.h>
//#include <linux/kthread.h>
//#include <linux/swap.h>
//#include <linux/timer.h>
//#include <linux/freezer.h>
//#include <linux/sched/signal.h>
//
//#include "f2fs.h"
#include "segment.h"
#include "node.h"
#include "gc.h"
//#include <trace/events/f2fs.h>
LOCAL_LOGGER_ENABLE(L"f2fs.segment", LOGGER_LEVEL_DEBUGINFO);

#define __reverse_ffz(x) __reverse_ffs(~(x))

//static struct kmem_cache *discard_entry_slab;
//static struct kmem_cache *discard_cmd_slab;
//static struct kmem_cache *sit_entry_set_slab;
//static struct kmem_cache *inmem_entry_slab;


static unsigned long __reverse_ulong(unsigned char *str)
{
	unsigned long tmp = 0;
	int shift = 24, idx = 0;

#if BITS_PER_LONG == 64
	shift = 56;
#endif
	while (shift >= 0) {
		tmp |= (unsigned long)str[idx++] << shift;
		shift -= BITS_PER_BYTE;
	}
	return tmp;
}

/* __reverse_ffs is copied from include/asm-generic/bitops/__ffs.h since MSB and LSB are reversed in a byte by 2fs_set_bit. */
static inline unsigned long __reverse_ffs(unsigned long word)
{
	int num = 0;

#if BITS_PER_LONG == 64
	if ((word & 0xffffffff00000000UL) == 0)
		num += 32;
	else
		word >>= 32;
#endif
	if ((word & 0xffff0000) == 0)	num += 16;
	else							word >>= 16;

	if ((word & 0xff00) == 0)		num += 8;
	else							word >>= 8;

	if ((word & 0xf0) == 0)			num += 4;
	else							word >>= 4;

	if ((word & 0xc) == 0)			num += 2;
	else							word >>= 2;

	if ((word & 0x2) == 0)			num += 1;
	return							num;
}

/*
 * __find_rev_next(_zero)_bit is copied from lib/find_next_bit.c because
 * f2fs_set_bit makes MSB and LSB reversed in a byte.
 * @size must be integral times of unsigned long.
 * Example:
 *                             MSB <--> LSB
 *   f2fs_set_bit(0, bitmap) => 1000 0000
 *   f2fs_set_bit(7, bitmap) => 0000 0001
 */
static unsigned long __find_rev_next_bit(const unsigned long *addr,
			unsigned long size, unsigned long offset)
{
	const unsigned long *p = addr + BIT_WORD(offset);
	unsigned long result = size;
	unsigned long tmp;

	if (offset >= size)
		return size;

	size -= (offset & ~(BITS_PER_LONG - 1));
	offset %= BITS_PER_LONG;

	while (1) {
		if (*p == 0)
			goto pass;

		tmp = __reverse_ulong((unsigned char *)p);

		tmp &= ~0UL >> offset;
		if (size < BITS_PER_LONG)
			tmp &= (~0UL << (BITS_PER_LONG - size));
		if (tmp)
			goto found;
pass:
		if (size <= BITS_PER_LONG)
			break;
		size -= BITS_PER_LONG;
		offset = 0;
		p++;
	}
	return result;
found:
	return result - size + __reverse_ffs(tmp);
}


static unsigned long __find_rev_next_zero_bit(const unsigned long *addr, unsigned long size, unsigned long offset)
{
	const unsigned long *p = addr + BIT_WORD(offset);
	unsigned long result = size;
	unsigned long tmp;

	if (offset >= size)
		return size;

	size -= (offset & ~(BITS_PER_LONG - 1));
	offset %= BITS_PER_LONG;

	while (1) {
		if (*p == ~0UL)
			goto pass;

		tmp = __reverse_ulong((unsigned char *)p);

		if (offset)
			tmp |= ~0UL << (BITS_PER_LONG - offset);
		if (size < BITS_PER_LONG)
			tmp |= ~0UL >> size;
		if (tmp != ~0UL)
			goto found;
pass:
		if (size <= BITS_PER_LONG)
			break;
		size -= BITS_PER_LONG;
		offset = 0;
		p++;
	}
	return result;
found:
	return result - size + __reverse_ffz(tmp);
}


bool f2fs_need_SSR(struct f2fs_sb_info *sbi)
{
	int node_secs = sbi->get_blocktype_secs(F2FS_DIRTY_NODES);
	int dent_secs = sbi->get_blocktype_secs(F2FS_DIRTY_DENTS);
	int imeta_secs = sbi->get_blocktype_secs(F2FS_DIRTY_IMETA);

	if (f2fs_lfs_mode(sbi))		return false;
	if (sbi->gc_mode == GC_URGENT_HIGH)		return true;
	if (unlikely(sbi->is_sbi_flag_set( SBI_CP_DISABLED)))		return true;

	return sbi->free_sections() <= (node_secs + 2 * dent_secs + imeta_secs + sbi->SM_I()->min_ssr_sections + sbi->reserved_sections());
}

void f2fs_register_inmem_page(inode *iinode, page *ppage)
{
	inmem_pages *new_page;

	if (PagePrivate(ppage))
		set_page_private(ppage, (unsigned long)ATOMIC_WRITTEN_PAGE);
	else
		f2fs_set_page_private(ppage, ATOMIC_WRITTEN_PAGE);

//	new_page = f2fs_kmem_cache_alloc<inmem_pages>(inmem_entry_slab, GFP_NOFS);
	new_page = f2fs_kmem_cache_alloc<inmem_pages>(NULL, GFP_NOFS);

	/* add atomic page indices to the list */
	new_page->page = ppage;
	INIT_LIST_HEAD(&new_page->list);

	/* increase reference count with clean state */
	ppage->get_page();
	mutex_lock(&F2FS_I(iinode)->inmem_lock);
	list_add_tail(&new_page->list, &F2FS_I(iinode)->inmem_pages);
	F2FS_I_SB(iinode)->inc_page_count( F2FS_INMEM_PAGES);
	mutex_unlock(&F2FS_I(iinode)->inmem_lock);

//	trace_f2fs_register_inmem_page(page, INMEM);
}

static int __revoke_inmem_pages(inode *iinode, list_head *head, bool drop, bool recover, bool trylock)
{
	f2fs_sb_info *sbi = F2FS_I_SB(iinode);
	inmem_pages *cur, *tmp;
	int err = 0;

	list_for_each_entry_safe(inmem_pages, cur, tmp, head, list) 
	{
		struct page *page = cur->page;

//		if (drop)		trace_f2fs_commit_inmem_page(page, INMEM_DROP);

		if (trylock) 
		{
			/* to avoid deadlock in between page lock and* inmem_lock.		 */
			if (!trylock_page(page))			continue;
		} 
		else {		lock_page(page);		}

		f2fs_wait_on_page_writeback(page, DATA, true, true);

		if (recover) 
		{
			dnode_of_data dn;
			node_info ni;

//			trace_f2fs_commit_inmem_page(page, INMEM_REVOKE);
retry:
			dn.set_new_dnode(iinode, NULL, NULL, 0);
			err = f2fs_get_dnode_of_data(&dn, page->index, LOOKUP_NODE);
			if (err) 
			{
				if (err == -ENOMEM) 
				{
//					congestion_wait(BLK_RW_ASYNC, DEFAULT_IO_TIMEOUT);
//					cond_resched();
					goto retry;
				}
				err = -EAGAIN;
				goto next;
			}

			err = NM_I(sbi)->f2fs_get_node_info( dn.nid, &ni);
			if (err) 
			{
				f2fs_put_dnode(&dn);
				return err;
			}

			if (cur->old_addr == NEW_ADDR) 
			{
				f2fs_invalidate_blocks(sbi, dn.data_blkaddr);
				f2fs_update_data_blkaddr(&dn, NEW_ADDR);
			}
			else f2fs_replace_block(sbi, &dn, dn.data_blkaddr, cur->old_addr, ni.version, true, true);
			f2fs_put_dnode(&dn);
		}
next:
		/* we don't need to invalidate this in the sccessful status */
		if (drop || recover) 
		{
			ClearPageUptodate(page);
			clear_cold_data(page);
		}
		f2fs_clear_page_private(page);
		f2fs_put_page(page, 1);

		list_del(&cur->list);
//		kmem_cache_free(inmem_entry_slab, cur);
		kmem_cache_free(NULL, cur);
		F2FS_I_SB(iinode)->dec_page_count(F2FS_INMEM_PAGES);
	}
	return err;
}

void f2fs_drop_inmem_pages_all(f2fs_sb_info *sbi, bool gc_failure)
{
//	list_head *head = &sbi->inode_list[ATOMIC_FILE];
//	inode *node;
	f2fs_inode_info *fi;
	unsigned int count = sbi->atomic_files;
	unsigned int looped = 0;
next:
	spin_lock(&sbi->inode_lock[ATOMIC_FILE]);
//	if (list_empty(head)) 
	if(sbi->list_empty(ATOMIC_FILE))
	{
		spin_unlock(&sbi->inode_lock[ATOMIC_FILE]);
		return;
	}
//	fi = list_first_entry(head, f2fs_inode_info, inmem_ilist);
	fi = sbi->get_list_first_entry(ATOMIC_FILE);
//	node = igrab(&fi->vfs_inode);
	inode* iinode = igrab(fi);		//增加引用计数
//	if (node) list_move_tail(&fi->inmem_ilist, head);
	if (fi) sbi->list_move_tail(fi, ATOMIC_FILE);
	spin_unlock(&sbi->inode_lock[ATOMIC_FILE]);

	if (fi) 
	{
		if (gc_failure) 
		{
			if (!fi->i_gc_failures[GC_FAILURE_ATOMIC])			goto skip;
		}
		fi->set_inode_flag(FI_ATOMIC_REVOKE_REQUEST);
		f2fs_drop_inmem_pages(fi);
skip:
		iput(iinode);
	}
#if 0 // TODO
	congestion_wait(BLK_RW_ASYNC, DEFAULT_IO_TIMEOUT);
	cond_resched();
#endif
	if (gc_failure) 
	{
		if (++looped >= count)		return;
	}
	goto next;
}

void f2fs_drop_inmem_pages(f2fs_inode_info*inode)
{
	f2fs_sb_info *sbi = F2FS_I_SB(inode);
	f2fs_inode_info *fi = F2FS_I(inode);

	do {
		mutex_lock(&fi->inmem_lock);
		if (list_empty(&fi->inmem_pages)) {
			fi->i_gc_failures[GC_FAILURE_ATOMIC] = 0;

			spin_lock(&sbi->inode_lock[ATOMIC_FILE]);
			if (!list_empty(&fi->inmem_ilist))
				list_del_init(&fi->inmem_ilist);
			if (f2fs_is_atomic_file(inode)) {
				clear_inode_flag(fi, FI_ATOMIC_FILE);
				sbi->atomic_files--;
			}
			spin_unlock(&sbi->inode_lock[ATOMIC_FILE]);

			mutex_unlock(&fi->inmem_lock);
			break;
		}
		__revoke_inmem_pages(inode, &fi->inmem_pages, true, false, true);
		mutex_unlock(&fi->inmem_lock);
	} while (1);
}

void f2fs_drop_inmem_page(f2fs_inode_info *inode, page *ppage)
{
//	f2fs_inode_info *fi = F2FS_I(inode);
	f2fs_sb_info *sbi = inode->m_sbi;
	list_head *head = &inode->inmem_pages;
	inmem_pages *cur = NULL;

	f2fs_bug_on(sbi, !IS_ATOMIC_WRITTEN_PAGE(ppage));

	mutex_lock(&inode->inmem_lock);
	list_for_each_entry(inmem_pages, cur, head, list) 
	{
		if (cur->page == ppage)		break;
	}

	f2fs_bug_on(sbi, list_empty(head) || cur->page != ppage);
	list_del(&cur->list);
	mutex_unlock(&inode->inmem_lock);

	sbi->dec_page_count(F2FS_INMEM_PAGES);
//	kmem_cache_free(inmem_entry_slab, cur);
	kmem_cache_free(NULL, cur);

	ClearPageUptodate(ppage);
	f2fs_clear_page_private(ppage);
	f2fs_put_page(ppage, 0);

//	trace_f2fs_commit_inmem_page(page, INMEM_INVALIDATE);
}

#if 0

static int __f2fs_commit_inmem_pages(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct inmem_pages *cur, *tmp;
	struct f2fs_io_info fio = {
		.sbi = sbi,
		.ino = inode->i_ino,
		.type = DATA,
		.op = REQ_OP_WRITE,
		.op_flags = REQ_SYNC | REQ_PRIO,
		.io_type = FS_DATA_IO,
	};
	struct list_head revoke_list;
	bool submit_bio = false;
	int err = 0;

	INIT_LIST_HEAD(&revoke_list);

	list_for_each_entry_safe(cur, tmp, &fi->inmem_pages, list) {
		struct page *page = cur->page;

		lock_page(page);
		if (page->mapping == inode->i_mapping) {
			trace_f2fs_commit_inmem_page(page, INMEM);

			f2fs_wait_on_page_writeback(page, DATA, true, true);

			set_page_dirty(page);
			if (clear_page_dirty_for_io(page)) 
			{
				F_LOG_DEBUG(L"page.dirty", L" dec: inode=%d, page=%d", inode->i_ino, page->index);
				inode_dec_dirty_pages(inode);
				f2fs_remove_dirty_inode(inode);
			}
retry:
			fio.page = page;
			fio.old_blkaddr = NULL_ADDR;
			fio.encrypted_page = NULL;
			fio.need_lock = LOCK_DONE;
			err = f2fs_do_write_data_page(&fio);
			if (err) {
				if (err == -ENOMEM) {
					congestion_wait(BLK_RW_ASYNC,
							DEFAULT_IO_TIMEOUT);
					cond_resched();
					goto retry;
				}
				unlock_page(page);
				break;
			}
			/* record old blkaddr for revoking */
			cur->old_addr = fio.old_blkaddr;
			submit_bio = true;
		}
		unlock_page(page);
		list_move_tail(&cur->list, &revoke_list);
	}

	if (submit_bio)
		f2fs_submit_merged_write_cond(sbi, inode, NULL, 0, DATA);

	if (err) {
		/*
		 * try to revoke all committed pages, but still we could fail
		 * due to no memory or other reason, if that happened, EAGAIN
		 * will be returned, which means in such case, transaction is
		 * already not integrity, caller should use journal to do the
		 * recovery or rewrite & commit last transaction. For other
		 * error number, revoking was done by filesystem itself.
		 */
		err = __revoke_inmem_pages(inode, &revoke_list,
						false, true, false);

		/* drop all uncommitted pages */
		__revoke_inmem_pages(inode, &fi->inmem_pages,
						true, false, false);
	} else {
		__revoke_inmem_pages(inode, &revoke_list,
						false, false, false);
	}

	return err;
}

int f2fs_commit_inmem_pages(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	int err;

	sbi->f2fs_balance_fs(true);

	down_write(&fi->i_gc_rwsem[WRITE]);

	sbi->f2fs_lock_op();
	//auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);

	set_inode_flag(inode, FI_ATOMIC_COMMIT);

	mutex_lock(&fi->inmem_lock);
	err = __f2fs_commit_inmem_pages(inode);
	mutex_unlock(&fi->inmem_lock);

	clear_inode_flag(inode, FI_ATOMIC_COMMIT);

	sbi->f2fs_unlock_op();
	up_write(&fi->i_gc_rwsem[WRITE]);

	return err;
}

#endif
/* This function balances dirty node and dentry pages. In addition, it controls garbage collection. */
//void f2fs_balance_fs(struct f2fs_sb_info *sbi, bool need)
void f2fs_sb_info::f2fs_balance_fs(bool need)
{
	if (time_to_inject(this, FAULT_CHECKPOINT))
	{
		f2fs_show_injection_info(this, FAULT_CHECKPOINT);
		f2fs_stop_checkpoint(this, false);
	}

	/* balance_fs_bg is able to be pending */
	if (need && excess_cached_nats(this))		f2fs_balance_fs_bg(this, false);
	if (!f2fs_is_checkpoint_ready())		return;

	/* We should do GC or end up with checkpoint, if there are so many dirty dir/node pages without enough free segments.*/
	if (has_not_enough_free_secs(0, 0)) 
	{
//		if (test_opt(sbi, GC_MERGE) && sbi->gc_thread && sbi->gc_thread->f2fs_gc_task) 
		if (test_opt(this, GC_MERGE) && gc_thread && gc_thread->IsRunning() ) 
		{
			//通过唤醒GC线程，触发GC。自身是否要等待？
#if 0
			DEFINE_WAIT(wait);
			prepare_to_wait(&this->gc_thread->fggc_wq, &wait, TASK_UNINTERRUPTIBLE);
			wake_up(&this->gc_thread->gc_wait_queue_head);
			io_schedule();
			finish_wait(&this->gc_thread->fggc_wq, &wait);
#else
			JCASSERT(0);
#endif
		} 
		else
		{
//			JCASSERT(0);
			down_write(&gc_lock);
			f2fs_gc(this, false, false, false, NULL_SEGNO);
		}
	}
}

void f2fs_balance_fs_bg(f2fs_sb_info *sbi, bool from_bg)
{
	if (unlikely(sbi->is_sbi_flag_set( SBI_POR_DOING)))
		return;

	/* try to shrink extent cache when there is no enough memory */
	if (!f2fs_available_free_memory(sbi, EXTENT_CACHE))
		f2fs_shrink_extent_tree(sbi, EXTENT_CACHE_SHRINK_NUMBER);

	/* check the # of cached NAT entries */
	if (!f2fs_available_free_memory(sbi, NAT_ENTRIES))
		f2fs_try_to_free_nats(sbi, NAT_ENTRY_PER_BLOCK);

	if (!f2fs_available_free_memory(sbi, FREE_NIDS))
		f2fs_try_to_free_nids(sbi, MAX_FREE_NIDS);
	else
		NM_I(sbi)->f2fs_build_free_nids(false, false);

	if (excess_dirty_nats(sbi) || excess_dirty_nodes(sbi) ||
		excess_prefree_segs(sbi))
		goto do_sync;

	/* there is background inflight IO or foreground operation recently */
#if 0 // TODO
	if (is_inflight_io(sbi, REQ_TIME) || (!f2fs_time_over(sbi, REQ_TIME) && rwsem_is_locked(&sbi->cp_rwsem)))
#endif
	if (is_inflight_io(sbi, REQ_TIME) || (!f2fs_time_over(sbi, REQ_TIME) ))
		return;

	/* exceed periodical checkpoint timeout threshold */
	if (f2fs_time_over(sbi, CP_TIME))
		goto do_sync;

	/* checkpoint is the only way to shrink partial cached entries */
	if (f2fs_available_free_memory(sbi, NAT_ENTRIES) ||
		f2fs_available_free_memory(sbi, INO_ENTRIES))
		return;

do_sync:
	if (test_opt(sbi, DATA_FLUSH) && from_bg) {
		struct blk_plug plug;

		mutex_lock(&sbi->flush_lock);

		blk_start_plug(&plug);
		f2fs_sync_dirty_inodes(sbi, FILE_INODE);
		blk_finish_plug(&plug);

		mutex_unlock(&sbi->flush_lock);
	}
	sbi->sync_fs(1);
	stat_inc_bg_cp_count(sbi->stat_info);
}

int f2fs_sb_info::__submit_flush_wait(block_device *bdev)
{
	int ret = m_fs->blkdev_issue_flush(bdev);
	//trace_f2fs_issue_flush(bdev, test_opt(sbi, NOBARRIER),	test_opt(sbi, FLUSH_MERGE), ret);
	return ret;
}


int f2fs_sb_info::submit_flush_wait(nid_t ino)
{
	int ret = 0;
	int i;

	if (!f2fs_is_multi_device())		return __submit_flush_wait(s_bdev);

	for (i = 0; i < s_ndevs; i++)
	{
		if (!f2fs_is_dirty_device(this, ino, i, FLUSH_INO))			continue;
		ret = __submit_flush_wait(devs[i].m_disk);
		if (ret)			break;
	}
	return ret;
}


//DWORD WINAPI flush_cmd_control::_issue_flush_thread(LPVOID data)
//{
//	//f2fs_sb_info* sbi = reinterpret_cast<f2fs_sb_info*>(data);
//	flush_cmd_control* fcc = reinterpret_cast<flush_cmd_control*>(data);
//	fcc->m_run = true;
//	return fcc->issue_flush_thread();
//}

DWORD flush_cmd_control::issue_flush_thread(void)
{
	LOG_STACK_TRACE()
//	flush_cmd_control *fcc = sm_info->fcc_info;
//#if 0
//	wait_queue_head_t *q = &flush_wait_queue;
//#endif
	while (m_running)
	{
	//#if 0
	//	if (kthread_should_stop())		return 0;
	//#endif

		if (!llist_empty(&issue_list))
		{
			LOG_DEBUG(L"processing issue list");
			flush_cmd* cmd, * next;
			int ret;

			dispatch_list = llist_del_all(&issue_list);				// 从issue_list移到dispatch_list
			dispatch_list = llist_reverse_order(dispatch_list);		// 交换链表中entry的顺数
			cmd = llist_entry(dispatch_list, flush_cmd, llnode);
			ret = m_sb_info->submit_flush_wait(cmd->ino);
			atomic_inc(&issued_flush);

			llist_for_each_entry_safe(flush_cmd, cmd, next, dispatch_list, llnode)
			{
				cmd->ret = ret;
				// Linux同步机制，唤醒另一个等待complete的线程
				complete(&cmd->wait);
				LOG_DEBUG(L"completed command");
			}
			dispatch_list = NULL;
		}
		LOG_DEBUG(L"waiting for issue list")
		WaitForSingleObject(m_que_event, INFINITE);
	//#if 0
	//	wait_event_interruptible(*q, kthread_should_stop() || !llist_empty(&fcc->issue_list));
	//#endif
	}
//	m_run = false;
	InterlockedExchange(&m_running, 0);
	return 0;
}

int flush_cmd_control::f2fs_issue_flush(nid_t ino)
{
	LOG_STACK_TRACE();
	f2fs_sb_info* sbi = m_sb_info;
//	struct flush_cmd_control *fcc = sbi->SM_I()->fcc_info;
	flush_cmd cmd;
	int ret;

	if (test_opt(sbi, NOBARRIER))		return 0;

	if (!test_opt(sbi, FLUSH_MERGE)) 
	{
		LOG_DEBUG(L"submit flush command without flush_merge, ino=%d", ino);
		atomic_inc(&queued_flush);
		ret = sbi->submit_flush_wait(ino);
		atomic_dec(&queued_flush);
		atomic_inc(&issued_flush);
		return ret;
	}

	if (atomic_inc_return(&queued_flush) == 1 ||  sbi->f2fs_is_multi_device())
	{
		LOG_DEBUG(L"submit flush command due to queue=1, ino=%d", ino);
		ret = sbi->submit_flush_wait(ino);
		atomic_dec(&queued_flush);
		atomic_inc(&issued_flush);
		return ret;
	}

	LOG_DEBUG(L"insert flush command, ino=%d", ino);
	cmd.ino = ino;
	init_completion(&cmd.wait);
	llist_add(&cmd.llnode, &issue_list);

	/* update issue_list before we wake up issue_flush thread, this smp_mb() pairs with another barrier in 
	___wait_event(), see more details in comments of waitqueue_active(). */
#if 0
	smp_mb();
#endif
	if (m_que_event) SetEvent(m_que_event);
//	if (waitqueue_active(&flush_wait_queue))		wake_up(&flush_wait_queue);
	if (m_thread) 
	{
		wait_for_completion(&cmd.wait);
		atomic_dec(&queued_flush);
		LOG_DEBUG(L"command completed");
	} 
	else 
	{
		//struct llist_node *list;
		llist_node * list = llist_del_all(&issue_list);
		if (!list) 
		{
			wait_for_completion(&cmd.wait);
			atomic_dec(&queued_flush);
			LOG_DEBUG(L"command completed");
		} 
		else 
		{
			flush_cmd *tmp, *next;
			ret = sbi->submit_flush_wait(ino);
			llist_for_each_entry_safe(flush_cmd, tmp, next, list, llnode) 
			{
				if (tmp == &cmd)
				{
					cmd.ret = ret;
					atomic_dec(&queued_flush);
					continue;
				}
				tmp->ret = ret;
				complete(&tmp->wait);
			}
		}
	}
	return cmd.ret;
}

int f2fs_sb_info::f2fs_create_flush_cmd_control(void)
{
	flush_cmd_control *fcc;
	int err = 0;
	if (sm_info->fcc_info) 
	{
		fcc = sm_info->fcc_info;
//		if (fcc->f2fs_issue_flush_thread)		return err;
		if (fcc->IsRunning())		return err;
//		goto init_thread;
	}
	else
	{
		//	fcc = f2fs_kzalloc<flush_cmd_control>(NULL, 1/*, GFP_KERNEL*/);
		fcc = new flush_cmd_control(this);
		if (!fcc)		THROW_ERROR(ERR_MEM, L"failed on new flush_cmd_control");
		//<YUAN>初始化部分移动到flush_cmd_control的构造函数中
	//	atomic_set(&fcc->issued_flush, 0);
	//	atomic_set(&fcc->queued_flush, 0);
	//#if 0
	//	init_waitqueue_head(&fcc->flush_wait_queue);
	//#endif
	//	init_llist_head(&fcc->issue_list);
		sm_info->fcc_info = fcc;
		if (!test_opt(this, FLUSH_MERGE))		return err;
	}
//init_thread:
//	fcc->f2fs_issue_flush_thread = kthread_run(issue_flush_thread, sbi, "f2fs_flush-%u:%u", MAJOR(dev), MINOR(dev));
	bool br = fcc->Start();

//	if (IS_ERR(fcc->f2fs_issue_flush)) {
	if (!br)
	{
//		err = PTR_ERR(fcc->f2fs_issue_flush);
		err = -EINVAL;
//		kfree(fcc);
		delete fcc;
		sm_info->fcc_info = NULL;
		return err;
	}
	return err;
}

flush_cmd_control::flush_cmd_control(f2fs_sb_info * sb_info)
{
	m_sb_info = sb_info;
	//m_que_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	//if (m_que_event == NULL || m_que_event == INVALID_HANDLE_VALUE)
	//{
	//	THROW_WIN32_ERROR(L"failed on creating event");
	//}
	//f2fs_issue_flush_thread = NULL;
	//thread_id = 0;
	//m_run = false;

	atomic_set(&issued_flush, 0);
	atomic_set(&queued_flush, 0);
//#if 0
//	init_waitqueue_head(&flush_wait_queue);
//#endif
	init_llist_head(&issue_list);
}

flush_cmd_control::~flush_cmd_control(void)
{
	//if (m_que_event)	CloseHandle(m_que_event);
	//m_que_event = NULL;
	//if (f2fs_issue_flush_thread)	CloseHandle(f2fs_issue_flush_thread);
	//f2fs_issue_flush_thread = NULL;
}

//bool flush_cmd_control::StartThread(void)
//{
//	f2fs_issue_flush_thread = CreateThread(NULL, 0, _issue_flush_thread, this, 0, &thread_id);
//	if (f2fs_issue_flush_thread == NULL || f2fs_issue_flush_thread == INVALID_HANDLE_VALUE)
//	{
//		THROW_WIN32_ERROR(L"failed on creating flush control thread");
//	}
//	return true;
//}

//void flush_cmd_control::StopThread(void)
//{
//	if (!is_running()) return;
//	InterlockedExchange(&m_run, 0);
//	SetEvent(m_que_event);
//	DWORD ir = WaitForSingleObject(f2fs_issue_flush_thread, 1000);
//	if (ir != 0 && ir != WAIT_TIMEOUT)	THROW_WIN32_ERROR(L"failed on stopping flush control thread");
//	CloseHandle(f2fs_issue_flush_thread);
//	f2fs_issue_flush_thread = NULL;
//}

void f2fs_sm_info::f2fs_destroy_flush_cmd_control(/*struct f2fs_sb_info *sbi,*/ bool free)
{
	//flush_cmd_control *fcc = sbi->SM_I()->fcc_info;
	fcc_info->Stop();
//	if (m_thread)
//	{
//		struct task_struct *flush_thread = fcc->f2fs_issue_flush;
//		Stop();
		//fcc->f2fs_issue_flush_thread = NULL;
		//kthread_stop(flush_thread);
//	}
	if (free) 
	{
		delete fcc_info;
		fcc_info = nullptr;

		//kfree(fcc);
		//sbi->SM_I()->fcc_info = NULL;
	}
}

int f2fs_flush_device_cache(struct f2fs_sb_info *sbi)
{
	int ret = 0, i;

	if (!sbi->f2fs_is_multi_device())		return 0;

	if (test_opt(sbi, NOBARRIER))		return 0;

	for (i = 1; i < sbi->s_ndevs; i++)
	{
		if (!f2fs_test_bit(i, (char *)&sbi->dirty_device))		continue;
		ret = sbi->__submit_flush_wait(sbi->devs[i].m_disk);
		if (ret)
			break;

		spin_lock(&sbi->dev_lock);
		f2fs_clear_bit(i, (char *)&sbi->dirty_device);
		spin_unlock(&sbi->dev_lock);
	}

	return ret;
}

static void __locate_dirty_segment(struct f2fs_sb_info *sbi, unsigned int segno, enum dirty_type dirty_type_val)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);

	/* need not be added */
	if (IS_CURSEG(sbi, segno))	return;

	if (!__test_and_set_bit(segno, dirty_i->dirty_segmap[dirty_type_val]))		dirty_i->nr_dirty[dirty_type_val]++;

	if (dirty_type_val == DIRTY) {
		struct seg_entry *sentry = sbi->get_seg_entry( segno);
		enum dirty_type t =(dirty_type)( sentry->type);

		if (unlikely(t >= DIRTY))
		{
			f2fs_bug_on(sbi, 1);
			return;
		}
		if (!__test_and_set_bit(segno, dirty_i->dirty_segmap[t]))			dirty_i->nr_dirty[t]++;

		if (sbi->__is_large_section()) 
		{
			unsigned int secno = GET_SEC_FROM_SEG(sbi, segno);
			block_t valid_blocks = get_valid_blocks(sbi, segno, true);

			f2fs_bug_on(sbi, unlikely(!valid_blocks ||	valid_blocks == BLKS_PER_SEC(sbi)));

			if (!IS_CURSEC(sbi, secno))	__set_bit(secno, dirty_i->dirty_secmap);
		}
	}
}
static void __remove_dirty_segment(f2fs_sb_info *sbi, unsigned int segno, enum dirty_type dirty_type_val)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	block_t valid_blocks;

	if (__test_and_clear_bit(segno, dirty_i->dirty_segmap[dirty_type_val]))
		dirty_i->nr_dirty[dirty_type_val]--;

	if (dirty_type_val == DIRTY) {
		struct seg_entry *sentry = sbi->get_seg_entry( segno);
		dirty_type t = (dirty_type)(sentry->type);

		if (__test_and_clear_bit(segno, dirty_i->dirty_segmap[t]))
			dirty_i->nr_dirty[t]--;

		valid_blocks = get_valid_blocks(sbi, segno, true);
		if (valid_blocks == 0) 
		{
			__clear_bit(GET_SEC_FROM_SEG(sbi, segno),	dirty_i->victim_secmap);
#ifdef CONFIG_F2FS_CHECK_FS
			__clear_bit(segno, sbi->SIT_I()->invalid_segmap);
#endif
		}
		if (sbi->__is_large_section()) 
		{
			unsigned int secno = GET_SEC_FROM_SEG(sbi, segno);
			if (!valid_blocks || valid_blocks == BLKS_PER_SEC(sbi)) 
			{
				__clear_bit(secno, dirty_i->dirty_secmap);
				return;
			}
			if (!IS_CURSEC(sbi, secno))			__set_bit(secno, dirty_i->dirty_secmap);
		}
	}
}

/*
 * Should not occur error such as -ENOMEM.
 * Adding dirty entry into seglist is not critical operation.
 * If a given segment is one of current working segments, it won't be added.
 */
static void locate_dirty_segment(struct f2fs_sb_info *sbi, unsigned int segno)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned short valid_blocks, ckpt_valid_blocks;
	unsigned int usable_blocks;

	if (segno == NULL_SEGNO || IS_CURSEG(sbi, segno))
		return;

	usable_blocks = f2fs_usable_blks_in_seg(sbi, segno);
	mutex_lock(&dirty_i->seglist_lock);

	valid_blocks = get_valid_blocks(sbi, segno, false);
	ckpt_valid_blocks = get_ckpt_valid_blocks(sbi, segno, false);

	if (valid_blocks == 0 && (!sbi->is_sbi_flag_set( SBI_CP_DISABLED) ||
		ckpt_valid_blocks == usable_blocks)) {
		__locate_dirty_segment(sbi, segno, PRE);
		__remove_dirty_segment(sbi, segno, DIRTY);
	} else if (valid_blocks < usable_blocks) {
		__locate_dirty_segment(sbi, segno, DIRTY);
	} else {
		/* Recovery routine with SSR needs this */
		__remove_dirty_segment(sbi, segno, DIRTY);
	}

	mutex_unlock(&dirty_i->seglist_lock);
}
#if 0

/* This moves currently empty dirty blocks to prefree. Must hold seglist_lock */
void f2fs_dirty_to_prefree(struct f2fs_sb_info *sbi)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned int segno;

	mutex_lock(&dirty_i->seglist_lock);
	for_each_set_bit(segno, dirty_i->dirty_segmap[DIRTY], sbi->MAIN_SEGS()) {
		if (get_valid_blocks(sbi, segno, false))
			continue;
		if (IS_CURSEG(sbi, segno))
			continue;
		__locate_dirty_segment(sbi, segno, PRE);
		__remove_dirty_segment(sbi, segno, DIRTY);
	}
	mutex_unlock(&dirty_i->seglist_lock);
}

block_t f2fs_get_unusable_blocks(struct f2fs_sb_info *sbi)
{
	int ovp_hole_segs =
		(overprovision_segments(sbi) - reserved_segments(sbi));
	block_t ovp_holes = ovp_hole_segs << sbi->log_blocks_per_seg;
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	block_t holes[2] = {0, 0};	/* DATA and NODE */
	block_t unusable;
	struct seg_entry *se;
	unsigned int segno;

	mutex_lock(&dirty_i->seglist_lock);
	for_each_set_bit(segno, dirty_i->dirty_segmap[DIRTY], sbi->MAIN_SEGS()) {
		se = sbi->get_seg_entry( segno);
		if (IS_NODESEG(se->type))
			holes[NODE] += f2fs_usable_blks_in_seg(sbi, segno) -
							se->valid_blocks;
		else
			holes[DATA] += f2fs_usable_blks_in_seg(sbi, segno) -
							se->valid_blocks;
	}
	mutex_unlock(&dirty_i->seglist_lock);

	unusable = holes[DATA] > holes[NODE] ? holes[DATA] : holes[NODE];
	if (unusable > ovp_holes)
		return unusable - ovp_holes;
	return 0;
}

int f2fs_disable_cp_again(struct f2fs_sb_info *sbi, block_t unusable)
{
	int ovp_hole_segs =
		(overprovision_segments(sbi) - reserved_segments(sbi));
	if (unusable > F2FS_OPTION(sbi).unusable_cap)
		return -EAGAIN;
	if (sbi->is_sbi_flag_set( SBI_CP_DISABLED_QUICK) &&
		dirty_segments(sbi) > ovp_hole_segs)
		return -EAGAIN;
	return 0;
}

#endif
/* This is only used by SBI_CP_DISABLED */
static unsigned int get_free_segment(struct f2fs_sb_info *sbi)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned int segno = 0;

	mutex_lock(&dirty_i->seglist_lock);
	for_each_set_bit(segno, dirty_i->dirty_segmap[DIRTY], sbi->MAIN_SEGS())
	{
		if (get_valid_blocks(sbi, segno, false))			continue;
		if (get_ckpt_valid_blocks(sbi, segno, false))			continue;
		mutex_unlock(&dirty_i->seglist_lock);
		return segno;
	}
	mutex_unlock(&dirty_i->seglist_lock);
	return NULL_SEGNO;
}
#if 0
#endif






#if 0

static void __update_discard_tree_range(struct f2fs_sb_info *sbi,
				block_device *bdev, block_t lstart,
				block_t start, block_t len);

#endif





//static void __punch_discard_cmd(struct f2fs_sb_info *sbi,  struct discard_cmd *dc, block_t blkaddr)
void discard_cmd_control::__punch_discard_cmd(discard_cmd *dc, block_t blkaddr)
{
//	discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	struct discard_info di = dc->di;
	bool modified = false;

	if (dc->state == D_DONE || dc->len == 1) 
	{
		__remove_discard_cmd(dc);
		return;
	}

	undiscard_blks -= di.len;

	if (blkaddr > di.lstart) {
		dc->len = blkaddr - dc->lstart;
		undiscard_blks += dc->len;
		__relocate_discard_cmd(dc);
		modified = true;
	}

	if (blkaddr < di.lstart + di.len - 1) 
	{
		if (modified) 
		{
			__insert_discard_tree(dc->bdev, blkaddr + 1,
					di.start + blkaddr + 1 - di.lstart,
					di.lstart + di.len - 1 - blkaddr,
					NULL, NULL);
		} else {
			dc->lstart++;
			dc->len--;
			dc->start++;
			undiscard_blks += dc->len;
			__relocate_discard_cmd(dc);
		}
	}
}


static int __queue_discard_cmd(f2fs_sb_info *sbi, block_device *bdev, block_t blkstart, block_t blklen)
{
	block_t lblkstart = blkstart;

	if (!f2fs_bdev_support_discard(bdev))
		return 0;

//	trace_f2fs_queue_discard(bdev, blkstart, blklen);

	if (sbi->f2fs_is_multi_device()) {
		int devi = f2fs_target_device_index(sbi, blkstart);

		blkstart -= FDEV(devi).start_blk;
	}
	mutex_lock(&sbi->SM_I()->dcc_info->cmd_lock);
	sbi->SM_I()->dcc_info->__update_discard_tree_range(bdev, lblkstart, blkstart, blklen);
	mutex_unlock(&sbi->SM_I()->dcc_info->cmd_lock);
	return 0;
}



#if 0
static unsigned int __wait_all_discard_cmd(struct f2fs_sb_info *sbi,
					struct discard_policy *dpolicy);

#endif

//static bool __drop_discard_cmd(struct f2fs_sb_info *sbi)
bool discard_cmd_control::__drop_discard_cmd(void)
{
//	discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	list_head *pend_list;
	discard_cmd *dc, *tmp;
	int i;
	bool dropped = false;

	mutex_lock(&this->cmd_lock);
	for (i = MAX_PLIST_NUM - 1; i >= 0; i--) 
	{
		pend_list = &this->pend_list[i];
		list_for_each_entry_safe(discard_cmd, dc, tmp, pend_list, list) 
		{
//			f2fs_bug_on(sbi, dc->state != D_PREP);
			JCASSERT(dc->state == D_PREP);
			__remove_discard_cmd(dc);
			dropped = true;
		}
	}
	mutex_unlock(&this->cmd_lock);
	return dropped;
}
#if 0

void f2fs_drop_discard_cmd(struct f2fs_sb_info *sbi)
{
	__drop_discard_cmd(sbi);
}

#endif


/* This should be covered by global mutex, &sit_i->sentry_lock */
void discard_cmd_control::f2fs_wait_discard_bio(block_t blkaddr)
{
//	discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	struct discard_cmd *dc;
	bool need_wait = false;

	mutex_lock(&cmd_lock);
	dc = (struct discard_cmd *)f2fs_lookup_rb_tree(&root, NULL, blkaddr);
	if (dc) {
		if (dc->state == D_PREP) 
		{
			__punch_discard_cmd(dc, blkaddr);
		} else {
			dc->ref++;
			need_wait = true;
		}
	}
	mutex_unlock(&cmd_lock);

	if (need_wait)		__wait_one_discard_bio(dc);
}

// 直接调用Stop()成员函数
//void discard_cmd_control::f2fs_stop_discard_thread(void)
//{
//	LOG_STACK_TRACE();
////	discard_cmd_control *dcc = sm_info->dcc_info;
//
////	if (dcc && dcc->f2fs_issue_discard) 
//	if (dcc && dcc->IsRunning())
//	{
////		struct task_struct *discard_thread = dcc->f2fs_issue_discard;
////		dcc->f2fs_issue_discard = NULL;
////		kthread_stop(discard_thread);
//		dcc->Stop();
//	}
//}

/* This comes from f2fs_put_super */
bool discard_cmd_control::f2fs_issue_discard_timeout(void)
{
//	discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	struct discard_policy dpolicy;
	bool dropped;

	__init_discard_policy(&dpolicy, DPOLICY_UMOUNT, discard_granularity);
	__issue_discard_cmd(&dpolicy);
	dropped = __drop_discard_cmd();

	/* just to make sure there is no pending discard commands */
	__wait_all_discard_cmd(NULL);

//	f2fs_bug_on(sbi, atomic_read(&dcc->discard_cmd_cnt));
	JCASSERT(atomic_read(&discard_cmd_cnt) == 0);
	return dropped;
}


#if 0

#ifdef CONFIG_BLK_DEV_ZONED
static int __f2fs_issue_discard_zone(struct f2fs_sb_info *sbi,
		block_device *bdev, block_t blkstart, block_t blklen)
{
	sector_t sector, nr_sects;
	block_t lblkstart = blkstart;
	int devi = 0;

	if (sbi->f2fs_is_multi_device()) {
		devi = f2fs_target_device_index(sbi, blkstart);
		if (blkstart < FDEV(devi).start_blk ||
		    blkstart > FDEV(devi).end_blk) {
			f2fs_err(sbi, "Invalid block %x", blkstart);
			return -EIO;
		}
		blkstart -= FDEV(devi).start_blk;
	}

	/* For sequential zones, reset the zone write pointer */
	if (f2fs_blkz_is_seq(sbi, devi, blkstart)) {
		sector = SECTOR_FROM_BLOCK(blkstart);
		nr_sects = SECTOR_FROM_BLOCK(blklen);

		if (sector & (bdev_zone_sectors(bdev) - 1) ||
				nr_sects != bdev_zone_sectors(bdev)) {
			f2fs_err(sbi, "(%d) %s: Unaligned zone reset attempted (block %x + %x)",
				 devi, sbi->s_ndevs ? FDEV(devi).path : "",
				 blkstart, blklen);
			return -EIO;
		}
		trace_f2fs_issue_reset_zone(bdev, blkstart);
		return blkdev_zone_mgmt(bdev, REQ_OP_ZONE_RESET,
					sector, nr_sects, GFP_NOFS);
	}

	/* For conventional zones, use regular discard if supported */
	return __queue_discard_cmd(sbi, bdev, lblkstart, blklen);
}
#endif
#endif //TODO

static int __issue_discard_async(struct f2fs_sb_info *sbi,
		block_device *bdev, block_t blkstart, block_t blklen)
{
#ifdef CONFIG_BLK_DEV_ZONED
	if (f2fs_sb_has_blkzoned(sbi) && bdev_is_zoned(bdev))
		return __f2fs_issue_discard_zone(sbi, bdev, blkstart, blklen);
#endif
	return __queue_discard_cmd(sbi, bdev, blkstart, blklen);
}


static int f2fs_issue_discard(f2fs_sb_info *sbi, block_t blkstart, block_t blklen)
{
	sector_t start = blkstart, len = 0;
	block_device *bdev;
	struct seg_entry *se;
	unsigned int offset;
	block_t i;
	int err = 0;

	bdev = sbi->f2fs_target_device(blkstart, NULL);

	for (i = blkstart; i < blkstart + blklen; i++, len++) {
		if (i != start) {
			block_device *bdev2 =
				sbi->f2fs_target_device(i, NULL);

			if (bdev2 != bdev) {
				err = __issue_discard_async(sbi, bdev, start, len);
				if (err)
					return err;
				bdev = bdev2;
				start = i;
				len = 0;
			}
		}

		se = sbi->get_seg_entry( GET_SEGNO(sbi, i));
		offset = GET_BLKOFF_FROM_SEG0(sbi, i);

		if (!f2fs_test_and_set_bit(offset, (char*)se->discard_map))
			sbi->discard_blks--;
	}

	if (len)
		err = __issue_discard_async(sbi, bdev, start, len);
	return err;
}

static bool add_discard_addrs(struct f2fs_sb_info *sbi, struct cp_control *cpc,	bool check_only)
{
	int entries = SIT_VBLOCK_MAP_SIZE / sizeof(unsigned long);
	int max_blocks = sbi->blocks_per_seg;
	struct seg_entry *se = sbi->get_seg_entry( cpc->trim_start);
	unsigned long *cur_map = (unsigned long *)se->cur_valid_map;
	unsigned long *ckpt_map = (unsigned long *)se->ckpt_valid_map;
	unsigned long *discard_map = (unsigned long *)se->discard_map;
	unsigned long *dmap = sbi->SIT_I()->tmp_map;
	unsigned int start = 0, end = -1;
	bool force = (cpc->reason & CP_DISCARD);
	discard_entry *de = NULL;
	f2fs_sm_info* sm_i = sbi->SM_I();
	struct list_head *head = &sm_i->dcc_info->entry_list;
	int i;

	if (se->valid_blocks == max_blocks || !f2fs_hw_support_discard(sbi)) return false;

	if (!force) 
	{
		if (!f2fs_realtime_discard_enable(sbi) || !se->valid_blocks ||
			sm_i->dcc_info->nr_discards >= sm_i->dcc_info->max_discards)
			return false;
	}

	/* SIT_VBLOCK_MAP_SIZE should be multiple of sizeof(unsigned long) */
	for (i = 0; i < entries; i++)
		dmap[i] = force ? ~ckpt_map[i] & ~discard_map[i] :
				(cur_map[i] ^ ckpt_map[i]) & ckpt_map[i];

	while (force || sm_i->dcc_info->nr_discards <= sm_i->dcc_info->max_discards) {
		start = __find_rev_next_bit(dmap, max_blocks, end + 1);
		if (start >= max_blocks)
			break;

		end = __find_rev_next_zero_bit(dmap, max_blocks, start + 1);
		if (force && start && end != max_blocks
					&& (end - start) < cpc->trim_minlen)
			continue;

		if (check_only)
			return true;

		if (!de) 
		{
			de = f2fs_kmem_cache_alloc<discard_entry>(/*discard_entry_slab*/NULL,	GFP_F2FS_ZERO);
			de->start_blkaddr = START_BLOCK(sbi, cpc->trim_start);
			list_add_tail(&de->list, head);
		}

		for (i = start; i < end; i++) __set_bit_le(i, de->discard_map);

		sm_i->dcc_info->nr_discards += end - start;
	}
	return false;
}


static void release_discard_addr(struct discard_entry *entry)
{
	list_del(&entry->list);
	kmem_cache_free<discard_entry>(/*discard_entry_slab*/NULL, entry);
}

void f2fs_release_discard_addrs(struct f2fs_sb_info *sbi)
{
	struct list_head *head = &(sbi->SM_I()->dcc_info->entry_list);
	struct discard_entry *entry, *this_entry;

	/* drop caches */
	list_for_each_entry_safe(discard_entry, entry, this_entry, head, list)
		release_discard_addr(entry);
}

/* Should call f2fs_clear_prefree_segments after checkpoint is done. */
static void set_prefree_as_free_segments(f2fs_sb_info *sbi)
{
	dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned int segno;

	mutex_lock(&dirty_i->seglist_lock);
	for_each_set_bit(segno, dirty_i->dirty_segmap[PRE], sbi->MAIN_SEGS())
		sbi->__set_test_and_free(segno, false);
	mutex_unlock(&dirty_i->seglist_lock);
}


void f2fs_clear_prefree_segments(f2fs_sb_info *sbi, struct cp_control *cpc)
{
	discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	struct list_head *head = &dcc->entry_list;
	struct discard_entry *entry, *this_entry;
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned long *prefree_map = dirty_i->dirty_segmap[PRE];
	unsigned int start = 0, end = -1;
	unsigned int secno, start_segno;
	bool force = (cpc->reason & CP_DISCARD);
	bool need_align = f2fs_lfs_mode(sbi) && sbi->__is_large_section();

	mutex_lock(&dirty_i->seglist_lock);

	while (1) {
		int i;

		if (need_align && end != -1)			end--;
		start = find_next_bit(prefree_map, sbi->MAIN_SEGS(), end + 1);
		if (start >= sbi->MAIN_SEGS())			break;
		end = find_next_zero_bit(prefree_map, sbi->MAIN_SEGS(),								start + 1);

		if (need_align) {
			start = round_down(start, sbi->segs_per_sec);
			end = round_up(end, sbi->segs_per_sec);
		}

		for (i = start; i < end; i++) 
		{
			if (__test_and_clear_bit(i, prefree_map))			dirty_i->nr_dirty[PRE]--;
		}

		if (!f2fs_realtime_discard_enable(sbi))		continue;

		if (force && start >= cpc->trim_start &&		(end - 1) <= cpc->trim_end)
				continue;

		if (!f2fs_lfs_mode(sbi) || !sbi->__is_large_section()) 
		{
			f2fs_issue_discard(sbi, START_BLOCK(sbi, start), (end - start) << sbi->log_blocks_per_seg);
			continue;
		}
next:
		secno = GET_SEC_FROM_SEG(sbi, start);
		start_segno = GET_SEG_FROM_SEC(sbi, secno);
		if (!IS_CURSEC(sbi, secno) &&
			!get_valid_blocks(sbi, start, true))
			f2fs_issue_discard(sbi, START_BLOCK(sbi, start_segno),
				sbi->segs_per_sec << sbi->log_blocks_per_seg);

		start = start_segno + sbi->segs_per_sec;
		if (start < end) 	goto next;
		else			end = start - 1;
	}
	mutex_unlock(&dirty_i->seglist_lock);

	/* send small discards */
	list_for_each_entry_safe(discard_entry, entry, this_entry, head, list)
	{
		unsigned int cur_pos = 0, next_pos, len, total_len = 0;
		bool is_valid = test_bit_le(0, entry->discard_map);

find_next:
		if (is_valid) {
			next_pos = find_next_zero_bit_le(entry->discard_map,
					sbi->blocks_per_seg, cur_pos);
			len = next_pos - cur_pos;

			if (f2fs_sb_has_blkzoned(sbi) ||
			    (force && len < cpc->trim_minlen))
				goto skip;

			f2fs_issue_discard(sbi, entry->start_blkaddr + cur_pos,
									len);
			total_len += len;
		} else {
			next_pos = find_next_bit_le(entry->discard_map,
					sbi->blocks_per_seg, cur_pos);
		}
skip:
		cur_pos = next_pos;
		is_valid = !is_valid;

		if (cur_pos < sbi->blocks_per_seg)
			goto find_next;

		release_discard_addr(entry);
		dcc->nr_discards -= total_len;
	}

	wake_up_discard_thread(sbi, false);
}

int f2fs_sb_info::create_discard_cmd_control(void)
{
	discard_cmd_control *dcc;
	int err = 0;

	if (sm_info->dcc_info) 
	{
		dcc = sm_info->dcc_info;
		goto init_thread;
	}

//	dcc = f2fs_kzalloc(sbi, sizeof(discard_cmd_control), GFP_KERNEL);
	dcc = new discard_cmd_control(this);
	if (!dcc)	return -ENOMEM;

	//<YUAN>移动到构造函数中
	//dcc->discard_granularity = DEFAULT_DISCARD_GRANULARITY;
	//INIT_LIST_HEAD(&dcc->entry_list);
	//for (i = 0; i < MAX_PLIST_NUM; i++)		INIT_LIST_HEAD(&dcc->pend_list[i]);
	//INIT_LIST_HEAD(&dcc->wait_list);
	//INIT_LIST_HEAD(&dcc->fstrim_list);
	//mutex_init(&dcc->cmd_lock);
	//atomic_set(&dcc->issued_discard, 0);
	//atomic_set(&dcc->queued_discard, 0);
	//atomic_set(&dcc->discard_cmd_cnt, 0);
	//dcc->nr_discards = 0;
	//dcc->max_discards = MAIN_SEGS() << log_blocks_per_seg;
	//dcc->undiscard_blks = 0;
	//dcc->next_pos = 0;
	//dcc->root = RB_ROOT_CACHED;
	//dcc->rbtree_check = false;

//	init_waitqueue_head(&dcc->discard_wait_queue);
	sm_info->dcc_info = dcc;
init_thread:
//	dcc->f2fs_issue_discard = kthread_run(issue_discard_thread, sbi, "f2fs_discard-%u:%u", MAJOR(dev), MINOR(dev));
	bool br = dcc->Start();
//	if (IS_ERR(dcc->f2fs_issue_discard)) 
//	if (dcc->f2fs_issue_discard == NULL)
	if (!br)
	{
//		err = PTR_ERR(dcc->f2fs_issue_discard);
		err = -EINVAL;
		delete dcc;
//		f2fs_kvfree(dcc);
//		kfree(dcc);
		sm_info->dcc_info = NULL;
		return err;
	}
	return err;
}

//static void destroy_discard_cmd_control(struct f2fs_sb_info *sbi)
void discard_cmd_control::destroy_discard_cmd_control()
{
//	discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
//	if (!dcc) return;
//	f2fs_stop_discard_thread(sbi);
	if (IsRunning()) Stop();
	/* Recovery can cache discard commands, so in error path of fill_super(), it needs to give a chance to handle them.*/
	if (unlikely(atomic_read(&discard_cmd_cnt)))
		f2fs_issue_discard_timeout();
	//kfree(dcc);
	//sbi->SM_I()->dcc_info = NULL;
}

//static bool __mark_sit_entry_dirty(struct f2fs_sb_info *sbi, unsigned int segno)
bool sit_info::__mark_sit_entry_dirty(unsigned int segno)
{
//	LOG_STACK_TRACE();
	if (!__test_and_set_bit(segno, dirty_sentries_bitmap)) 
	{
		dirty_sentries++;
		return false;
	}
	return true;
}

//static void __set_sit_entry_type(struct f2fs_sb_info *sbi, int type, unsigned int segno, int modified)
void sit_info::__set_sit_entry_type(int type, unsigned int segno, int modified)
{
	LOG_STACK_TRACE();

	seg_entry *se = get_seg_entry(segno);
	se->type = type;
	if (modified)	__mark_sit_entry_dirty(segno);
}

static inline unsigned long long get_segment_mtime(f2fs_sb_info *sbi, block_t blkaddr)
{
	unsigned int segno = GET_SEGNO(sbi, blkaddr);

	if (segno == NULL_SEGNO)
		return 0;
	return sbi->get_seg_entry( segno)->mtime;
}

static void update_segment_mtime(struct f2fs_sb_info *sbi, block_t blkaddr,
						unsigned long long old_mtime)
{
	struct seg_entry *se;
	unsigned int segno = GET_SEGNO(sbi, blkaddr);
	unsigned long long ctime = get_mtime(sbi, false);
	unsigned long long mtime = old_mtime ? old_mtime : ctime;

	if (segno == NULL_SEGNO)
		return;

	se = sbi->get_seg_entry( segno);

	if (!se->mtime)
		se->mtime = mtime;
		/*se->mtime = div_u64(se->mtime * se->valid_blocks + mtime,	se->valid_blocks + 1);*/
	else	se->mtime = (se->mtime * se->valid_blocks + mtime) / (se->valid_blocks + 1);

	if (ctime >sbi->SIT_I()->max_mtime)		sbi->SIT_I()->max_mtime = ctime;
}
static void update_sit_entry(struct f2fs_sb_info *sbi, block_t blkaddr, int del)
{
	LOG_STACK_TRACE();

	struct seg_entry *se;
	unsigned int segno, offset;
	long int new_vblocks;
	bool exist;
#ifdef CONFIG_F2FS_CHECK_FS
	bool mir_exist;
#endif

	segno = GET_SEGNO(sbi, blkaddr);

	se = sbi->get_seg_entry( segno);
	new_vblocks = se->valid_blocks + del;
	offset = GET_BLKOFF_FROM_SEG0(sbi, blkaddr);

	f2fs_bug_on(sbi, (new_vblocks < 0 || (new_vblocks > f2fs_usable_blks_in_seg(sbi, segno))));

	se->valid_blocks = new_vblocks;

	/* Update valid block bitmap */
	if (del > 0) {
		exist = f2fs_test_and_set_bit(offset, (char*)se->cur_valid_map);
#ifdef CONFIG_F2FS_CHECK_FS
		mir_exist = f2fs_test_and_set_bit(offset,
						se->cur_valid_map_mir);
		if (unlikely(exist != mir_exist)) {
			f2fs_err(sbi, "Inconsistent error when setting bitmap, blk:%u, old bit:%d",
				 blkaddr, exist);
			f2fs_bug_on(sbi, 1);
		}
#endif
		if (unlikely(exist)) {
			f2fs_err(sbi, L"Bitmap was wrongly set, blk:%u",
				 blkaddr);
			f2fs_bug_on(sbi, 1);
			se->valid_blocks--;
			del = 0;
		}

		if (!f2fs_test_and_set_bit(offset, (char*)se->discard_map))
			sbi->discard_blks--;

		/*
		 * SSR should never reuse block which is checkpointed
		 * or newly invalidated.
		 */
		if (!sbi->is_sbi_flag_set( SBI_CP_DISABLED)) {
			if (!f2fs_test_and_set_bit(offset, (char*)se->ckpt_valid_map))
				se->ckpt_valid_blocks++;
		}
	} else {
		exist = f2fs_test_and_clear_bit(offset, (char*)se->cur_valid_map);
#ifdef CONFIG_F2FS_CHECK_FS
		mir_exist = f2fs_test_and_clear_bit(offset,
						se->cur_valid_map_mir);
		if (unlikely(exist != mir_exist)) {
			f2fs_err(sbi, "Inconsistent error when clearing bitmap, blk:%u, old bit:%d",
				 blkaddr, exist);
			f2fs_bug_on(sbi, 1);
		}
#endif
		if (unlikely(!exist)) {
			f2fs_err(sbi, L"Bitmap was wrongly cleared, blk:%u",blkaddr);
			f2fs_bug_on(sbi, 1);
			se->valid_blocks++;
			del = 0;
		} else if (unlikely(sbi->is_sbi_flag_set( SBI_CP_DISABLED))) {
			/*
			 * If checkpoints are off, we must not reuse data that
			 * was used in the previous checkpoint. If it was used
			 * before, we must track that to know how much space we
			 * really have.
			 */
			if (f2fs_test_bit(offset, se->ckpt_valid_map)) {
				spin_lock(&sbi->stat_lock);
				sbi->unusable_block_count++;
				spin_unlock(&sbi->stat_lock);
			}
		}

		if (f2fs_test_and_clear_bit(offset, (char*)se->discard_map))
			sbi->discard_blks++;
	}
	if (!f2fs_test_bit(offset, se->ckpt_valid_map))
		se->ckpt_valid_blocks += del;

	sbi->sm_info->sit_info->__mark_sit_entry_dirty(segno);

	/* update total number of valid blocks to be written in ckpt area */
	sbi->SIT_I()->written_valid_blocks += del;

	if (sbi->__is_large_section())
		get_sec_entry(sbi, segno)->valid_blocks += del;
}
void f2fs_invalidate_blocks(struct f2fs_sb_info *sbi, block_t addr)
{
	unsigned int segno = GET_SEGNO(sbi, addr);
	struct sit_info *sit_i = sbi->SIT_I();

	f2fs_bug_on(sbi, addr == NULL_ADDR);
	if (addr == NEW_ADDR || addr == COMPRESS_ADDR)
		return;

	invalidate_mapping_pages(META_MAPPING(sbi), addr, addr);

	/* add it into sit main buffer */
	down_write(&sit_i->sentry_lock);

	update_segment_mtime(sbi, addr, 0);
	update_sit_entry(sbi, addr, -1);

	/* add it into dirty seglist */
	locate_dirty_segment(sbi, segno);

	up_write(&sit_i->sentry_lock);
}

bool f2fs_is_checkpointed_data(struct f2fs_sb_info *sbi, block_t blkaddr)
{
	struct sit_info *sit_i = sbi->SIT_I();
	unsigned int segno, offset;
	struct seg_entry *se;
	bool is_cp = false;

	if (!__is_valid_data_blkaddr(blkaddr))
		return true;

	down_read(&sit_i->sentry_lock);

	segno = GET_SEGNO(sbi, blkaddr);
	se = sbi->get_seg_entry( segno);
	offset = GET_BLKOFF_FROM_SEG0(sbi, blkaddr);

	if (f2fs_test_bit(offset, se->ckpt_valid_map))
		is_cp = true;

	up_read(&sit_i->sentry_lock);

	return is_cp;
}

/*
 * This function should be resided under the curseg_mutex lock
 */
static void __add_sum_entry(f2fs_sb_info *sbi, int type, f2fs_summary *sum)
{
	curseg_info *curseg = sbi->CURSEG_I( type);
	BYTE *addr = (BYTE*)(&curseg->sum_blk);

	addr += curseg->next_blkoff * sizeof(struct f2fs_summary);
	memcpy(addr, sum, sizeof(struct f2fs_summary));
}

/* Calculate the number of current summary pages for writing */
int f2fs_sb_info::f2fs_npages_for_summary_flush(bool for_ra)
{
	int valid_sum_count = 0;
	int i, sum_in_page;

	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++) 
	{
		if (ckpt->alloc_type[i] == SSR)			valid_sum_count += blocks_per_seg;
		else 
		{
			if (for_ra)			valid_sum_count += le16_to_cpu(	F2FS_CKPT()->cur_data_blkoff[i]);
			else				valid_sum_count += curseg_blkoff(i);
		}
	}

	sum_in_page = (PAGE_SIZE - 2 * SUM_JOURNAL_SIZE - SUM_FOOTER_SIZE) / SUMMARY_SIZE;
	if (valid_sum_count <= sum_in_page)		return 1;
	else if ((valid_sum_count - sum_in_page) <=	(PAGE_SIZE - SUM_FOOTER_SIZE) / SUMMARY_SIZE)		return 2;
	return 3;
}

/* Caller should put this summary page */
struct page *f2fs_get_sum_page(struct f2fs_sb_info *sbi, unsigned int segno)
{
	if (unlikely(sbi->f2fs_cp_error()))		return (page*)(-EIO);
	return f2fs_get_meta_page_retry(sbi, GET_SUM_BLOCK(sbi, segno));
}

void f2fs_update_meta_page(struct f2fs_sb_info *sbi, void *src, block_t blk_addr)
{
	page *page = f2fs_grab_meta_page(sbi, blk_addr);
	memcpy(page_address<BYTE>(page), src, PAGE_SIZE);
	set_page_dirty(page);
	f2fs_put_page(page, 1);
}
#if 0

#endif
static void write_sum_page(struct f2fs_sb_info *sbi, struct f2fs_summary_block *sum_blk, block_t blk_addr)
{
	f2fs_update_meta_page(sbi, (void *)sum_blk, blk_addr);
}

static void write_current_sum_page(struct f2fs_sb_info *sbi,		int type, block_t blk_addr)
{
	curseg_info *curseg = sbi->CURSEG_I( type);
	struct page *page = f2fs_grab_meta_page(sbi, blk_addr);
	f2fs_summary_block *src = & curseg->sum_blk;
	f2fs_summary_block *dst;

	dst = page_address<f2fs_summary_block>(page);
	memset(dst, 0, PAGE_SIZE);

	mutex_lock(&curseg->curseg_mutex);

	down_read(&curseg->journal_rwsem);
	memcpy(&dst->journal, &curseg->journal, SUM_JOURNAL_SIZE);		//<CHECK>原代码中，复制目标为 &dst->journal
	up_read(&curseg->journal_rwsem);

	memcpy(dst->entries, src->entries, SUM_ENTRY_SIZE);
	memcpy(&dst->footer, &src->footer, SUM_FOOTER_SIZE);

	mutex_unlock(&curseg->curseg_mutex);

	set_page_dirty(page);
	f2fs_put_page(page, 1);
}

static int is_next_segment_free(struct f2fs_sb_info *sbi,	struct curseg_info *curseg, int type)
{
	unsigned int segno = curseg->segno + 1;
	struct free_segmap_info *free_i = sbi->FREE_I();

	if (segno < sbi->MAIN_SEGS() && segno % sbi->segs_per_sec)
		return !__test_bit(segno, free_i->free_segmap);
	return 0;
}

/*Find a new segment from the free segments bitmap to right order This function should be returned with success, otherwise BUG*/
static void get_new_segment(struct f2fs_sb_info *sbi, unsigned int *newseg, bool new_sec, int dir)
{
	struct free_segmap_info *free_i = sbi->FREE_I();
	unsigned int segno, secno, zoneno;
	unsigned int total_zones = sbi->MAIN_SECS() / sbi->secs_per_zone;
	unsigned int hint = GET_SEC_FROM_SEG(sbi, *newseg);
	unsigned int old_zoneno = GET_ZONE_FROM_SEG(sbi, *newseg);
	unsigned int left_start = hint;
	bool init = true;
	int go_left = 0;
	int i;

	spin_lock(&free_i->segmap_lock);

	if (!new_sec && ((*newseg + 1) % sbi->segs_per_sec)) 
	{
		segno = find_next_zero_bit(free_i->free_segmap, GET_SEG_FROM_SEC(sbi, hint + 1), *newseg + 1);
		if (segno < GET_SEG_FROM_SEC(sbi, hint + 1))		goto got_it;
	}
find_other_zone:
	secno = find_next_zero_bit(free_i->free_secmap, sbi->MAIN_SECS(), hint);
	if (secno >= sbi->MAIN_SECS())
	{
		if (dir == ALLOC_RIGHT) {
			secno = find_next_zero_bit(free_i->free_secmap, sbi->MAIN_SECS(), 0);
			f2fs_bug_on(sbi, secno >= sbi->MAIN_SECS());
		} else {
			go_left = 1;
			left_start = hint - 1;
		}
	}
	if (go_left == 0)
		goto skip_left;

	while (__test_bit(left_start, free_i->free_secmap)) 
	{
		if (left_start > 0) 
		{
			left_start--;
			continue;
		}
		left_start = find_next_zero_bit(free_i->free_secmap, sbi->MAIN_SECS(), 0);
		f2fs_bug_on(sbi, left_start >= sbi->MAIN_SECS());
		break;
	}
	secno = left_start;
skip_left:
	segno = GET_SEG_FROM_SEC(sbi, secno);
	zoneno = GET_ZONE_FROM_SEC(sbi, secno);

	/* give up on finding another zone */
	if (!init)
		goto got_it;
	if (sbi->secs_per_zone == 1)
		goto got_it;
	if (zoneno == old_zoneno)
		goto got_it;
	if (dir == ALLOC_LEFT) {
		if (!go_left && zoneno + 1 >= total_zones)
			goto got_it;
		if (go_left && zoneno == 0)
			goto got_it;
	}
	for (i = 0; i < NR_CURSEG_TYPE; i++)
		if (sbi->CURSEG_I(i)->zone == zoneno)
			break;

	if (i < NR_CURSEG_TYPE) {
		/* zone is in user, try another */
		if (go_left)
			hint = zoneno * sbi->secs_per_zone - 1;
		else if (zoneno + 1 >= total_zones)
			hint = 0;
		else
			hint = (zoneno + 1) * sbi->secs_per_zone;
		init = false;
		goto find_other_zone;
	}
got_it:
	/* set it as dirty segment in free segmap */
	f2fs_bug_on(sbi, __test_bit(segno, free_i->free_segmap));
	sbi->__set_inuse(segno);
	*newseg = segno;
	spin_unlock(&free_i->segmap_lock);
}

static void reset_curseg(f2fs_sb_info *sbi, int type, int modified)
{
	curseg_info *curseg = sbi->CURSEG_I(type);
	summary_footer *sum_footer;
	unsigned short seg_type = curseg->seg_type;

	curseg->inited = true;
	curseg->segno = curseg->next_segno;
	curseg->zone = GET_ZONE_FROM_SEG(sbi, curseg->segno);
	curseg->next_blkoff = 0;
	curseg->next_segno = NULL_SEGNO;

	sum_footer = &(curseg->sum_blk.footer);
	memset(sum_footer, 0, sizeof(summary_footer));

	sanity_check_seg_type(sbi, seg_type);

	if (IS_DATASEG(seg_type))		SET_SUM_TYPE(sum_footer, SUM_TYPE_DATA);
	if (IS_NODESEG(seg_type))		SET_SUM_TYPE(sum_footer, SUM_TYPE_NODE);
	sbi->sm_info->sit_info->__set_sit_entry_type(seg_type, curseg->segno, modified);
}

static unsigned int __get_next_segno(struct f2fs_sb_info *sbi, int type)
{
	struct curseg_info *curseg = sbi->CURSEG_I(type);
	unsigned short seg_type = curseg->seg_type;

	sanity_check_seg_type(sbi, seg_type);

	/* if segs_per_sec is large than 1, we need to keep original policy. */
	if (sbi->__is_large_section())		return curseg->segno;

	/* inmem log may not locate on any segment after mount */
	if (!curseg->inited)
		return 0;

	if (unlikely(sbi->is_sbi_flag_set( SBI_CP_DISABLED)))
		return 0;

	if (test_opt(sbi, NOHEAP) &&
		(seg_type == CURSEG_HOT_DATA || IS_NODESEG(seg_type)))
		return 0;

	if (sbi->SIT_I()->last_victim[ALLOC_NEXT])
		return sbi->SIT_I()->last_victim[ALLOC_NEXT];

	/* find segments from 0 to reuse freed segments */
	if (F2FS_OPTION(sbi).alloc_mode == ALLOC_MODE_REUSE)
		return 0;

	return curseg->segno;
}

/* Allocate a current working segment. This function always allocates a free segment in LFS manner. */
static void new_curseg(struct f2fs_sb_info *sbi, int type, bool new_sec)
{
	struct curseg_info *curseg = sbi->CURSEG_I(type);
	unsigned short seg_type = curseg->seg_type;
	unsigned int segno = curseg->segno;
	int dir = ALLOC_LEFT;

	if (curseg->inited)		write_sum_page(sbi, &curseg->sum_blk,	GET_SUM_BLOCK(sbi, segno));
	if (seg_type == CURSEG_WARM_DATA || seg_type == CURSEG_COLD_DATA)		dir = ALLOC_RIGHT;

	if (test_opt(sbi, NOHEAP))		dir = ALLOC_RIGHT;

	segno = __get_next_segno(sbi, type);
	get_new_segment(sbi, &segno, new_sec, dir);
	curseg->next_segno = segno;
	reset_curseg(sbi, type, 1);
	curseg->alloc_type = LFS;
}


static int __next_free_blkoff(f2fs_sb_info *sbi,	int segno, block_t start)
{
	struct seg_entry *se = sbi->get_seg_entry( segno);
	int entries = SIT_VBLOCK_MAP_SIZE / sizeof(unsigned long);
	unsigned long *target_map = sbi->SIT_I()->tmp_map;
	unsigned long *ckpt_map = (unsigned long *)se->ckpt_valid_map;
	unsigned long *cur_map = (unsigned long *)se->cur_valid_map;
	int i;

	for (i = 0; i < entries; i++)	target_map[i] = ckpt_map[i] | cur_map[i];
	return __find_rev_next_zero_bit(target_map, sbi->blocks_per_seg, start);
}

/* If a segment is written by LFS manner, next block offset is just obtained by increasing the current block offset. 
   However, if a segment is written by SSR manner, next block offset obtained by calling __next_free_blkoff */
static void __refresh_next_blkoff(f2fs_sb_info *sbi, curseg_info *seg)
{
	if (seg->alloc_type == SSR)
		seg->next_blkoff = __next_free_blkoff(sbi, seg->segno, seg->next_blkoff + 1);
	else
		seg->next_blkoff++;
}

bool f2fs_segment_has_free_slot(struct f2fs_sb_info *sbi, int segno)
{
	return __next_free_blkoff(sbi, segno, 0) < (int)(sbi->blocks_per_seg);
}

/* This function always allocates a used segment(from dirty seglist) by SSR manner, so it should recover the existing segment information of valid blocks */
static void change_curseg(f2fs_sb_info *sbi, int type, bool flush)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	struct curseg_info *curseg = sbi->CURSEG_I(type);
	unsigned int new_segno = curseg->next_segno;
	struct f2fs_summary_block *sum_node;
	struct page *sum_page;

	if (flush)
		write_sum_page(sbi, &curseg->sum_blk, GET_SUM_BLOCK(sbi, curseg->segno));

	sbi->__set_test_and_inuse(new_segno);

	mutex_lock(&dirty_i->seglist_lock);
	__remove_dirty_segment(sbi, new_segno, PRE);
	__remove_dirty_segment(sbi, new_segno, DIRTY);
	mutex_unlock(&dirty_i->seglist_lock);

	reset_curseg(sbi, type, 1);
	curseg->alloc_type = SSR;
	curseg->next_blkoff = __next_free_blkoff(sbi, curseg->segno, 0);

	sum_page = f2fs_get_sum_page(sbi, new_segno);
	if (IS_ERR(sum_page)) 
	{
		/* GC won't be able to use stale summary pages by cp_error */
		memset(&curseg->sum_blk, 0, SUM_ENTRY_SIZE);
		return;
	}
	sum_node = page_address<f2fs_summary_block>(sum_page);
	memcpy(&curseg->sum_blk, sum_node, SUM_ENTRY_SIZE);
	f2fs_put_page(sum_page, 1);
}

static int get_ssr_segment(f2fs_sb_info *sbi, int type, int alloc_mode, unsigned long long age);

static void get_atssr_segment(f2fs_sb_info *sbi, int type, int target_type, int alloc_mode,	unsigned long long age)
{
	curseg_info *curseg = sbi->CURSEG_I( type);
	curseg->seg_type = target_type;
	if (get_ssr_segment(sbi, type, alloc_mode, age)) 
	{
		seg_entry *se = sbi->get_seg_entry( curseg->next_segno);
		curseg->seg_type = se->type;
		change_curseg(sbi, type, true);
	} 
	else 
	{	/* allocate cold segment by default */
		curseg->seg_type = CURSEG_COLD_DATA;
		new_curseg(sbi, type, true);
	}
	stat_inc_seg_type(sbi, curseg);
}

static void __f2fs_init_atgc_curseg(f2fs_sb_info *sbi)
{
	curseg_info *curseg = sbi->CURSEG_I(CURSEG_ALL_DATA_ATGC);
	
	if (!sbi->am.atgc_enabled)	return;

	down_read(&sbi->SM_I()->curseg_lock);

	mutex_lock(&curseg->curseg_mutex);
	down_write(&sbi->SIT_I()->sentry_lock);

	get_atssr_segment(sbi, CURSEG_ALL_DATA_ATGC, CURSEG_COLD_DATA, SSR, 0);

	up_write(&sbi->SIT_I()->sentry_lock);
	mutex_unlock(&curseg->curseg_mutex);

	up_read(&sbi->SM_I()->curseg_lock);

}
void f2fs_init_inmem_curseg(struct f2fs_sb_info *sbi)
{
	__f2fs_init_atgc_curseg(sbi);
}

static void __f2fs_save_inmem_curseg(f2fs_sb_info *sbi, int type)
{
	curseg_info *curseg = sbi->CURSEG_I( type);

	mutex_lock(&curseg->curseg_mutex);
	if (!curseg->inited)	goto out;

	if (get_valid_blocks(sbi, curseg->segno, false))
	{
		write_sum_page(sbi, &curseg->sum_blk, GET_SUM_BLOCK(sbi, curseg->segno));
	} 
	else
	{
		mutex_lock(&DIRTY_I(sbi)->seglist_lock);
		sbi->__set_test_and_free(curseg->segno, true);
		mutex_unlock(&DIRTY_I(sbi)->seglist_lock);
	}
out:
	mutex_unlock(&curseg->curseg_mutex);
}

void f2fs_save_inmem_curseg(struct f2fs_sb_info *sbi)
{
	__f2fs_save_inmem_curseg(sbi, CURSEG_COLD_DATA_PINNED);

	if (sbi->am.atgc_enabled)
		__f2fs_save_inmem_curseg(sbi, CURSEG_ALL_DATA_ATGC);
}

static void __f2fs_restore_inmem_curseg(struct f2fs_sb_info *sbi, int type)
{
	struct curseg_info *curseg = sbi->CURSEG_I( type);

	mutex_lock(&curseg->curseg_mutex);
	if (!curseg->inited)
		goto out;
	if (get_valid_blocks(sbi, curseg->segno, false))
		goto out;

	mutex_lock(&DIRTY_I(sbi)->seglist_lock);
	sbi->__set_test_and_inuse(curseg->segno);
	mutex_unlock(&DIRTY_I(sbi)->seglist_lock);
out:
	mutex_unlock(&curseg->curseg_mutex);
}

void f2fs_restore_inmem_curseg(struct f2fs_sb_info *sbi)
{
	__f2fs_restore_inmem_curseg(sbi, CURSEG_COLD_DATA_PINNED);

	if (sbi->am.atgc_enabled)
		__f2fs_restore_inmem_curseg(sbi, CURSEG_ALL_DATA_ATGC);
}


static int get_ssr_segment(struct f2fs_sb_info *sbi, int type,		int alloc_mode, unsigned long long age)
{
	struct curseg_info *curseg = sbi->CURSEG_I(type);
	const struct victim_selection *v_ops = DIRTY_I(sbi)->v_ops;
	unsigned segno = NULL_SEGNO;
	unsigned short seg_type = curseg->seg_type;
	int i, cnt;
	bool reversed = false;

	sanity_check_seg_type(sbi, seg_type);

	/* f2fs_need_SSR() already forces to do this */
	if (!v_ops->get_victim(sbi, &segno, BG_GC, seg_type, alloc_mode, age)) {
		curseg->next_segno = segno;
		return 1;
	}

	/* For node segments, let's do SSR more intensively */
	if (IS_NODESEG(seg_type)) {
		if (seg_type >= CURSEG_WARM_NODE) {
			reversed = true;
			i = CURSEG_COLD_NODE;
		} else {
			i = CURSEG_HOT_NODE;
		}
		cnt = NR_CURSEG_NODE_TYPE;
	} else {
		if (seg_type >= CURSEG_WARM_DATA) {
			reversed = true;
			i = CURSEG_COLD_DATA;
		} else {
			i = CURSEG_HOT_DATA;
		}
		cnt = NR_CURSEG_DATA_TYPE;
	}

	for (; cnt-- > 0; reversed ? i-- : i++) {
		if (i == seg_type)
			continue;
		if (!v_ops->get_victim(sbi, &segno, BG_GC, i, alloc_mode, age)) {
			curseg->next_segno = segno;
			return 1;
		}
	}

	/* find valid_blocks=0 in dirty list */
	if (unlikely(sbi->is_sbi_flag_set( SBI_CP_DISABLED))) {
		segno = get_free_segment(sbi);
		if (segno != NULL_SEGNO) {
			curseg->next_segno = segno;
			return 1;
		}
	}
	return 0;
}
/* flush out current segment and replace it with new segment This function should be returned with success, otherwise BUG */
void sit_info::allocate_segment(f2fs_sb_info* sbi, int type, bool force)
{
	sbi->allocate_segment_by_default(type, force);
}

void f2fs_sb_info::allocate_segment_by_default(int type, bool force)
{
	struct curseg_info *curseg = CURSEG_I(type);

	if (force)		new_curseg(this, type, true);
	else if (!is_set_ckpt_flags(CP_CRC_RECOVERY_FLAG) && curseg->seg_type == CURSEG_WARM_NODE)	
					new_curseg(this, type, false);
	else if (curseg->alloc_type == LFS && is_next_segment_free(this, curseg, type) && likely(!is_sbi_flag_set(SBI_CP_DISABLED)))
					new_curseg(this, type, false);
	else if (f2fs_need_SSR(this) &&	get_ssr_segment(this, type, SSR, 0)) 
					change_curseg(this, type, true);
	else			new_curseg(this, type, false);
	stat_inc_seg_type(this, curseg);
}
#if 0

void f2fs_allocate_segment_for_resize(struct f2fs_sb_info *sbi, int type, unsigned int start, unsigned int end)
{
	struct curseg_info *curseg = sbi->CURSEG_I( type);
	unsigned int segno;

	down_read(&sbi->SM_I()->curseg_lock);
	mutex_lock(&curseg->curseg_mutex);
	down_write(&sbi->SIT_I()->sentry_lock);

	segno = sbi->CURSEG_I( type)->segno;
	if (segno < start || segno > end)
		goto unlock;

	if (f2fs_need_SSR(sbi) && get_ssr_segment(sbi, type, SSR, 0))
		change_curseg(sbi, type, true);
	else
		new_curseg(sbi, type, true);

	stat_inc_seg_type(sbi, curseg);

	locate_dirty_segment(sbi, segno);
unlock:
	up_write(&sbi->SIT_I()->sentry_lock);

	if (segno != curseg->segno)
		f2fs_notice(sbi, "For resize: curseg of type %d: %u ==> %u",
			    type, segno, curseg->segno);

	mutex_unlock(&curseg->curseg_mutex);
	up_read(&sbi->SM_I()->curseg_lock);
}
#endif
static void __allocate_new_segment(f2fs_sb_info *sbi, int type, bool new_sec, bool force)
{
	struct curseg_info *curseg = sbi->CURSEG_I(type);
	unsigned int old_segno;

	if (!curseg->inited) goto alloc;

	if (force || curseg->next_blkoff || get_valid_blocks(sbi, curseg->segno, new_sec))
		goto alloc;

	if (!get_ckpt_valid_blocks(sbi, curseg->segno, new_sec)) return;
alloc:
	old_segno = curseg->segno;
//	SIT_I(sbi)->s_ops->allocate_segment(sbi, type, true);
	sbi->SIT_I()->allocate_segment(sbi, type, true);
	locate_dirty_segment(sbi, old_segno);
}

static void __allocate_new_section(struct f2fs_sb_info *sbi, int type, bool force)
{
	__allocate_new_segment(sbi, type, true, force);
}

void f2fs_allocate_new_section(struct f2fs_sb_info *sbi, int type, bool force)
{
	down_read(&sbi->SM_I()->curseg_lock);
	down_write(&sbi->SIT_I()->sentry_lock);
	__allocate_new_section(sbi, type, force);
	up_write(&sbi->SIT_I()->sentry_lock);
	up_read(&sbi->SM_I()->curseg_lock);
}

void f2fs_allocate_new_segments(struct f2fs_sb_info *sbi)
{
	int i;

	down_read(&sbi->SM_I()->curseg_lock);
	down_write(&sbi->SIT_I()->sentry_lock);
	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++)
		__allocate_new_segment(sbi, i, false, false);
	up_write(&sbi->SIT_I()->sentry_lock);
	up_read(&sbi->SM_I()->curseg_lock);
}
//static const struct segment_allocation default_salloc_ops = {
//	.allocate_segment = allocate_segment_by_default,
//};

bool f2fs_exist_trim_candidates(struct f2fs_sb_info *sbi,
						struct cp_control *cpc)
{
	__u64 trim_start = cpc->trim_start;
	bool has_candidate = false;

	down_write(&sbi->SIT_I()->sentry_lock);
	for (; cpc->trim_start <= cpc->trim_end; cpc->trim_start++) {
		if (add_discard_addrs(sbi, cpc, true)) {
			has_candidate = true;
			break;
		}
	}
	up_write(&sbi->SIT_I()->sentry_lock);

	cpc->trim_start = trim_start;
	return has_candidate;
}
#if 0

static unsigned int __issue_discard_cmd_range(struct f2fs_sb_info *sbi,
					struct discard_policy *dpolicy,
					unsigned int start, unsigned int end)
{
	discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	struct discard_cmd *prev_dc = NULL, *next_dc = NULL;
	struct rb_node **insert_p = NULL, *insert_parent = NULL;
	struct discard_cmd *dc;
	struct blk_plug plug;
	int issued;
	unsigned int trimmed = 0;

next:
	issued = 0;

	mutex_lock(&dcc->cmd_lock);
	if (unlikely(dcc->rbtree_check))
		f2fs_bug_on(sbi, !f2fs_check_rb_tree_consistence(sbi,
							&dcc->root, false));

	dc = (struct discard_cmd *)f2fs_lookup_rb_tree_ret(&dcc->root,
					NULL, start,
					(struct rb_entry **)&prev_dc,
					(struct rb_entry **)&next_dc,
					&insert_p, &insert_parent, true, NULL);
	if (!dc)
		dc = next_dc;

	blk_start_plug(&plug);

	while (dc && dc->lstart <= end) {
		struct rb_node *node;
		int err = 0;

		if (dc->len < dpolicy->granularity)
			goto skip;

		if (dc->state != D_PREP) {
			list_move_tail(&dc->list, &dcc->fstrim_list);
			goto skip;
		}

		err = __submit_discard_cmd(sbi, dpolicy, dc, &issued);

		if (issued >= dpolicy->max_requests) {
			start = dc->lstart + dc->len;

			if (err)
				__remove_discard_cmd(sbi, dc);

			blk_finish_plug(&plug);
			mutex_unlock(&dcc->cmd_lock);
			trimmed += __wait_all_discard_cmd(sbi, NULL);
			congestion_wait(BLK_RW_ASYNC, DEFAULT_IO_TIMEOUT);
			goto next;
		}
skip:
		node = rb_next(&dc->rb_node);
		if (err)
			__remove_discard_cmd(sbi, dc);
		dc = rb_entry_safe(node, struct discard_cmd, rb_node);

		if (fatal_signal_pending(current))
			break;
	}

	blk_finish_plug(&plug);
	mutex_unlock(&dcc->cmd_lock);

	return trimmed;
}

int f2fs_trim_fs(struct f2fs_sb_info *sbi, struct fstrim_range *range)
{
	__u64 start = F2FS_BYTES_TO_BLK(range->start);
	__u64 end = start + F2FS_BYTES_TO_BLK(range->len) - 1;
	unsigned int start_segno, end_segno;
	block_t start_block, end_block;
	struct cp_control cpc;
	struct discard_policy dpolicy;
	unsigned long long trimmed = 0;
	int err = 0;
	bool need_align = f2fs_lfs_mode(sbi) && sbi->__is_large_section();

	if (start >= MAX_BLKADDR(sbi) || range->len < sbi->blocksize)
		return -EINVAL;

	if (end < MAIN_BLKADDR(sbi))
		goto out;

	if (sbi->is_sbi_flag_set( SBI_NEED_FSCK)) {
		f2fs_warn(sbi, "Found FS corruption, run fsck to fix.");
		return -EFSCORRUPTED;
	}

	/* start/end segment number in main_area */
	start_segno = (start <= MAIN_BLKADDR(sbi)) ? 0 : GET_SEGNO(sbi, start);
	end_segno = (end >= MAX_BLKADDR(sbi)) ? sbi->MAIN_SEGS() - 1 :
						GET_SEGNO(sbi, end);
	if (need_align) {
		start_segno = round_down(start_segno, sbi->segs_per_sec);
		end_segno = roundup(end_segno + 1, sbi->segs_per_sec) - 1;
	}

	cpc.reason = CP_DISCARD;
	cpc.trim_minlen = max_t(__u64, 1, F2FS_BYTES_TO_BLK(range->minlen));
	cpc.trim_start = start_segno;
	cpc.trim_end = end_segno;

	if (sbi->discard_blks == 0)
		goto out;

	down_write(&sbi->gc_lock);
	err = sbi->f2fs_write_checkpoint( &cpc);
	up_write(&sbi->gc_lock);
	if (err)
		goto out;

	/*
	 * We filed discard candidates, but actually we don't need to wait for
	 * all of them, since they'll be issued in idle time along with runtime
	 * discard option. User configuration looks like using runtime discard
	 * or periodic fstrim instead of it.
	 */
	if (f2fs_realtime_discard_enable(sbi))
		goto out;

	start_block = START_BLOCK(sbi, start_segno);
	end_block = START_BLOCK(sbi, end_segno + 1);

	__init_discard_policy(sbi, &dpolicy, DPOLICY_FSTRIM, cpc.trim_minlen);
	trimmed = __issue_discard_cmd_range(sbi, &dpolicy,
					start_block, end_block);

	trimmed += __wait_discard_cmd_range(sbi, &dpolicy,
					start_block, end_block);
out:
	if (!err)
		range->len = F2FS_BLK_TO_BYTES(trimmed);
	return err;
}
#endif

static bool __has_curseg_space(f2fs_sb_info *sbi, curseg_info *curseg)
{
	return curseg->next_blkoff < f2fs_usable_blks_in_seg(sbi, curseg->segno);
}

int f2fs_rw_hint_to_seg_type(enum rw_hint hint)
{
	switch (hint) {
	case WRITE_LIFE_SHORT:
		return CURSEG_HOT_DATA;
	case WRITE_LIFE_EXTREME:
		return CURSEG_COLD_DATA;
	default:
		return CURSEG_WARM_DATA;
	}
}

/* This returns write hints for each segment type. This hints will be
 * passed down to block layer. There are mapping tables which depend on
 * the mount option 'whint_mode'.
 *
 * 1) whint_mode=off. F2FS only passes down WRITE_LIFE_NOT_SET.
 *
 * 2) whint_mode=user-based. F2FS tries to pass down hints given by users.
 *
 * User                  F2FS                     Block
 * ----                  ----                     -----
 *                       META                     WRITE_LIFE_NOT_SET
 *                       HOT_NODE                 "
 *                       WARM_NODE                "
 *                       COLD_NODE                "
 * ioctl(COLD)           COLD_DATA                WRITE_LIFE_EXTREME
 * extension list        "                        "
 *
 * -- buffered io
 * WRITE_LIFE_EXTREME    COLD_DATA                WRITE_LIFE_EXTREME
 * WRITE_LIFE_SHORT      HOT_DATA                 WRITE_LIFE_SHORT
 * WRITE_LIFE_NOT_SET    WARM_DATA                WRITE_LIFE_NOT_SET
 * WRITE_LIFE_NONE       "                        "
 * WRITE_LIFE_MEDIUM     "                        "
 * WRITE_LIFE_LONG       "                        "
 *
 * -- direct io
 * WRITE_LIFE_EXTREME    COLD_DATA                WRITE_LIFE_EXTREME
 * WRITE_LIFE_SHORT      HOT_DATA                 WRITE_LIFE_SHORT
 * WRITE_LIFE_NOT_SET    WARM_DATA                WRITE_LIFE_NOT_SET
 * WRITE_LIFE_NONE       "                        WRITE_LIFE_NONE
 * WRITE_LIFE_MEDIUM     "                        WRITE_LIFE_MEDIUM
 * WRITE_LIFE_LONG       "                        WRITE_LIFE_LONG
 *
 * 3) whint_mode=fs-based. F2FS passes down hints with its policy.
 *
 * User                  F2FS                     Block
 * ----                  ----                     -----
 *                       META                     WRITE_LIFE_MEDIUM;
 *                       HOT_NODE                 WRITE_LIFE_NOT_SET
 *                       WARM_NODE                "
 *                       COLD_NODE                WRITE_LIFE_NONE
 * ioctl(COLD)           COLD_DATA                WRITE_LIFE_EXTREME
 * extension list        "                        "
 *
 * -- buffered io
 * WRITE_LIFE_EXTREME    COLD_DATA                WRITE_LIFE_EXTREME
 * WRITE_LIFE_SHORT      HOT_DATA                 WRITE_LIFE_SHORT
 * WRITE_LIFE_NOT_SET    WARM_DATA                WRITE_LIFE_LONG
 * WRITE_LIFE_NONE       "                        "
 * WRITE_LIFE_MEDIUM     "                        "
 * WRITE_LIFE_LONG       "                        "
 *
 * -- direct io
 * WRITE_LIFE_EXTREME    COLD_DATA                WRITE_LIFE_EXTREME
 * WRITE_LIFE_SHORT      HOT_DATA                 WRITE_LIFE_SHORT
 * WRITE_LIFE_NOT_SET    WARM_DATA                WRITE_LIFE_NOT_SET
 * WRITE_LIFE_NONE       "                        WRITE_LIFE_NONE
 * WRITE_LIFE_MEDIUM     "                        WRITE_LIFE_MEDIUM
 * WRITE_LIFE_LONG       "                        WRITE_LIFE_LONG
 */

enum rw_hint f2fs_io_type_to_rw_hint(f2fs_sb_info *sbi,	enum page_type type, enum temp_type temp)
{
	if (F2FS_OPTION(sbi).whint_mode == WHINT_MODE_USER) 
	{
		if (type == DATA) 
		{
			if (temp == WARM)				return WRITE_LIFE_NOT_SET;
			else if (temp == HOT)			return WRITE_LIFE_SHORT;
			else if (temp == COLD)			return WRITE_LIFE_EXTREME;
		} 
		else 
		{
			return WRITE_LIFE_NOT_SET;
		}
	} 
	else if (F2FS_OPTION(sbi).whint_mode == WHINT_MODE_FS) 
	{
		if (type == DATA) 
		{
			if (temp == WARM)				return WRITE_LIFE_LONG;
			else if (temp == HOT)			return WRITE_LIFE_SHORT;
			else if (temp == COLD)			return WRITE_LIFE_EXTREME;
		} 
		else if (type == NODE) 
		{
			if (temp == WARM || temp == HOT)	return WRITE_LIFE_NOT_SET;
			else if (temp == COLD)				return WRITE_LIFE_NONE;
		} 
		else if (type == META) {			return WRITE_LIFE_MEDIUM;		}
	}
	return WRITE_LIFE_NOT_SET;
}

static int __get_segment_type_2(struct f2fs_io_info *fio)
{
	if (fio->type == DATA)
		return CURSEG_HOT_DATA;
	else
		return CURSEG_HOT_NODE;
}

static int __get_segment_type_4(struct f2fs_io_info *fio)
{
	if (fio->type == DATA) {
		struct inode *inode = fio->page->mapping->host;

		if (S_ISDIR(inode->i_mode))
			return CURSEG_HOT_DATA;
		else
			return CURSEG_COLD_DATA;
	} else {
		if (IS_DNODE(fio->page) && is_cold_node(fio->page))
			return CURSEG_WARM_NODE;
		else
			return CURSEG_COLD_NODE;
	}
}

static int __get_segment_type_6(struct f2fs_io_info *fio)
{
	if (fio->type == DATA)
	{
		inode *iinode = fio->page->mapping->host;
		f2fs_inode_info* fi = F2FS_I(iinode);

		if (is_cold_data(fio->page)) {
			if (fio->sbi->am.atgc_enabled &&
				(fio->io_type == FS_DATA_IO) &&
				(fio->sbi->gc_mode != GC_URGENT_HIGH))
				return CURSEG_ALL_DATA_ATGC;
			else
				return CURSEG_COLD_DATA;
		}
		if (file_is_cold(iinode) || f2fs_need_compress_data(fi))
			return CURSEG_COLD_DATA;
		if (file_is_hot(iinode) || is_inode_flag_set(fi, FI_HOT_DATA) || f2fs_is_atomic_file(fi) || f2fs_is_volatile_file(fi))
			return CURSEG_HOT_DATA;
		return f2fs_rw_hint_to_seg_type((rw_hint)(iinode->i_write_hint));
	} 
	else
	{
		if (IS_DNODE(fio->page))
			return is_cold_node(fio->page) ? CURSEG_WARM_NODE : CURSEG_HOT_NODE;
		return CURSEG_COLD_NODE;
	}
}

static int __get_segment_type(struct f2fs_io_info *fio)
{
	int type = 0;

	switch (F2FS_OPTION(fio->sbi).active_logs) {
	case 2:
		type = __get_segment_type_2(fio);
		break;
	case 4:
		type = __get_segment_type_4(fio);
		break;
	case 6:
		type = __get_segment_type_6(fio);
		break;
	default:
		f2fs_bug_on(fio->sbi, true);
	}

	if (IS_HOT(type))
		fio->temp = HOT;
	else if (IS_WARM(type))
		fio->temp = WARM;
	else
		fio->temp = COLD;
	return type;
}

void f2fs_allocate_data_block(f2fs_sb_info *sbi, page *page, block_t old_blkaddr, block_t *new_blkaddr,
		struct f2fs_summary *sum, int type,
		struct f2fs_io_info *fio)
{
	discard_cmd_control *dcc = sbi->SM_I()->dcc_info;

	struct sit_info *sit_i = sbi->SIT_I();
	struct curseg_info *curseg = sbi->CURSEG_I( type);
	unsigned long long old_mtime;
	bool from_gc = (type == CURSEG_ALL_DATA_ATGC);
	struct seg_entry *se = NULL;

	down_read(&sbi->SM_I()->curseg_lock);

	mutex_lock(&curseg->curseg_mutex);
	down_write(&sit_i->sentry_lock);

	if (from_gc) {
		f2fs_bug_on(sbi, GET_SEGNO(sbi, old_blkaddr) == NULL_SEGNO);
		se = sbi->get_seg_entry( GET_SEGNO(sbi, old_blkaddr));
		sanity_check_seg_type(sbi, se->type);
		f2fs_bug_on(sbi, IS_NODESEG(se->type));
	}
	*new_blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);

	f2fs_bug_on(sbi, curseg->next_blkoff >= sbi->blocks_per_seg);

	dcc->f2fs_wait_discard_bio( *new_blkaddr);

	/* __add_sum_entry should be resided under the curseg_mutex because, this function updates a summary entry in the
	 * current summary block. */
	__add_sum_entry(sbi, type, sum);

	__refresh_next_blkoff(sbi, curseg);

	stat_inc_block_count(sbi, curseg);

	if (from_gc) {
		old_mtime = get_segment_mtime(sbi, old_blkaddr);
	} else {
		update_segment_mtime(sbi, old_blkaddr, 0);
		old_mtime = 0;
	}
	update_segment_mtime(sbi, *new_blkaddr, old_mtime);

	/*
	 * SIT information should be updated before segment allocation,
	 * since SSR needs latest valid block information.
	 */
	update_sit_entry(sbi, *new_blkaddr, 1);
	if (GET_SEGNO(sbi, old_blkaddr) != NULL_SEGNO)
		update_sit_entry(sbi, old_blkaddr, -1);

	if (!__has_curseg_space(sbi, curseg)) {
		if (from_gc)
			get_atssr_segment(sbi, type, se->type,
				AT_SSR, se->mtime);
		//else	sit_i->s_ops->allocate_segment(sbi, type, false);
		else sit_i->allocate_segment(sbi, type, false);
	}

	/*
	 * segment dirty status should be updated after segment allocation,
	 * so we just need to update status only one time after previous
	 * segment being closed.
	 */
	locate_dirty_segment(sbi, GET_SEGNO(sbi, old_blkaddr));
	locate_dirty_segment(sbi, GET_SEGNO(sbi, *new_blkaddr));

	up_write(&sit_i->sentry_lock);

	if (page && IS_NODESEG(type)) {
		fill_node_footer_blkaddr(page, NEXT_FREE_BLKADDR(sbi, curseg));

		f2fs_inode_chksum_set(sbi, page);
	}

	if (fio) {
		struct f2fs_bio_info *io;

		if (F2FS_IO_ALIGNED(sbi))
			fio->retry = false;

		INIT_LIST_HEAD(&fio->list);
		fio->in_list = true;
		io = sbi->write_io[fio->type] + fio->temp;
		spin_lock(&io->io_lock);
		list_add_tail(&fio->list, &io->io_list);
		spin_unlock(&io->io_lock);
	}

	mutex_unlock(&curseg->curseg_mutex);

	up_read(&sbi->SM_I()->curseg_lock);
}

static void update_device_state(struct f2fs_io_info *fio)
{
	struct f2fs_sb_info *sbi = fio->sbi;
	unsigned int devidx;

	if (!sbi->f2fs_is_multi_device())
		return;

	devidx = f2fs_target_device_index(sbi, fio->new_blkaddr);

	/* update device state for fsync */
	f2fs_set_dirty_device(sbi, fio->ino, devidx, FLUSH_INO);

	/* update device state for checkpoint */
	if (!f2fs_test_bit(devidx, (char *)&sbi->dirty_device)) {
		spin_lock(&sbi->dev_lock);
		f2fs_set_bit(devidx, (char *)&sbi->dirty_device);
		spin_unlock(&sbi->dev_lock);
	}
}

static void do_write_page(f2fs_summary *sum, f2fs_io_info *fio)
{
	int type = __get_segment_type(fio);
	bool keep_order = (f2fs_lfs_mode(fio->sbi) && type == CURSEG_COLD_DATA);

	if (keep_order)	down_read(&fio->sbi->io_order_lock);
reallocate:
	f2fs_allocate_data_block(fio->sbi, fio->page, fio->old_blkaddr,	&fio->new_blkaddr, sum, type, fio);
	if (GET_SEGNO(fio->sbi, fio->old_blkaddr) != NULL_SEGNO)
		invalidate_mapping_pages(META_MAPPING(fio->sbi), fio->old_blkaddr, fio->old_blkaddr);

	/* writeout dirty page into bdev */
	fio->sbi->f2fs_submit_page_write(fio);
	if (fio->retry) 
	{
		fio->old_blkaddr = fio->new_blkaddr;
		goto reallocate;
	}
	update_device_state(fio);
	if (keep_order)	up_read(&fio->sbi->io_order_lock);
}


//void f2fs_do_write_meta_page(f2fs_sb_info *sbi,  page *ppage, enum iostat_type io_type)
void f2fs_sb_info::f2fs_do_write_meta_page(page *ppage, enum iostat_type io_type)
{
	f2fs_io_info fio;
	fio.sbi = this;
	fio.type = META;
	fio.temp = HOT;
	fio.op = REQ_OP_WRITE;
	fio.op_flags = REQ_SYNC | REQ_META | REQ_PRIO;
	fio.old_blkaddr = ppage->index;
	fio.new_blkaddr = ppage->index;
	fio.page = ppage;
	fio.encrypted_page = NULL;
	fio.in_list = false;

	if (unlikely(ppage->index >= MAIN_BLKADDR(this)))	fio.op_flags &= ~REQ_META;

	set_page_writeback(ppage);
	ClearPageError(ppage);
	f2fs_submit_page_write(&fio);

	stat_inc_meta_count(this, ppage->index);
	f2fs_update_iostat(this, io_type, F2FS_BLKSIZE);
}

void f2fs_do_write_node_page(unsigned int nid, f2fs_io_info *fio)
{
	f2fs_summary sum;
	set_summary(&sum, nid, 0, 0);
	do_write_page(&sum, fio);
	f2fs_update_iostat(fio->sbi, fio->io_type, F2FS_BLKSIZE);
}


void f2fs_outplace_write_data(struct dnode_of_data *dn,	struct f2fs_io_info *fio)
{
	struct f2fs_sb_info *sbi = fio->sbi;
	struct f2fs_summary sum;

	f2fs_bug_on(sbi, dn->data_blkaddr == NULL_ADDR);
	set_summary(&sum, dn->nid, dn->ofs_in_node, fio->version);
	do_write_page(&sum, fio);
	f2fs_update_data_blkaddr(dn, fio->new_blkaddr);

	f2fs_update_iostat(sbi, fio->io_type, F2FS_BLKSIZE);
}

int f2fs_inplace_write_data(struct f2fs_io_info *fio)
{
	int err;
	struct f2fs_sb_info *sbi = fio->sbi;
	unsigned int segno;

	fio->new_blkaddr = fio->old_blkaddr;
	/* i/o temperature is needed for passing down write hints */
	__get_segment_type(fio);

	segno = GET_SEGNO(sbi, fio->new_blkaddr);

	if (!IS_DATASEG(sbi->get_seg_entry( segno)->type))
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		LOG_WARNING(L"incorrect segment(% u) type, run fsck to fix.", segno);
		err = -EFSCORRUPTED;
		goto drop_bio;
	}

	if (sbi->is_sbi_flag_set( SBI_NEED_FSCK) || sbi->f2fs_cp_error())
	{
		err = -EIO;
		goto drop_bio;
	}

	stat_inc_inplace_blocks(fio->sbi);

	if (fio->bio && !(sbi->SM_I()->ipu_policy & (1 << F2FS_IPU_NOCACHE)))		err = f2fs_merge_page_bio(fio);
	else		err = sbi->f2fs_submit_page_bio(fio);
	if (!err)
	{
		update_device_state(fio);
		f2fs_update_iostat(fio->sbi, fio->io_type, F2FS_BLKSIZE);
	}

	return err;
drop_bio:
	if (fio->bio && *(fio->bio)) 
	{
		struct bio *bio = *(fio->bio);
		bio->bi_status = BLK_STS_IOERR;
#if 0 //TODO
		bio_endio(bio);
#else
		JCASSERT(0);
#endif
		*(fio->bio) = NULL;
	}
	return err;
}

inline int f2fs_sb_info::__f2fs_get_curseg(unsigned int segno)
{
	int i;
	for (i = CURSEG_HOT_DATA; i < NO_CHECK_TYPE; i++) 
	{
		if (CURSEG_I( i)->segno == segno)	break;
	}
	return i;
}

//void f2fs_do_replace_block(struct f2fs_sb_info *sbi, struct f2fs_summary *sum,
//				block_t old_blkaddr, block_t new_blkaddr,
//				bool recover_curseg, bool recover_newaddr,
//				bool from_gc)
void f2fs_sb_info::f2fs_do_replace_block(f2fs_summary* sum, block_t old_blkaddr, block_t new_blkaddr,
		bool recover_curseg, bool recover_newaddr, bool from_gc)
{
	struct sit_info *sit_i = this->SIT_I();
	struct curseg_info *curseg;
	unsigned int segno, old_cursegno;
	struct seg_entry *se;
	int type;
	unsigned short old_blkoff;
	unsigned char old_alloc_type;

	segno = GET_SEGNO(this, new_blkaddr);
	se = this->get_seg_entry( segno);
	type = se->type;

	down_write(&this->SM_I()->curseg_lock);

	if (!recover_curseg) 
	{
		/* for recovery flow */
		if (se->valid_blocks == 0 && !IS_CURSEG(this, segno)) 
		{
			if (old_blkaddr == NULL_ADDR)		type = CURSEG_COLD_DATA;
			else				type = CURSEG_WARM_DATA;
		}
	}
	else
	{
		if (IS_CURSEG(this, segno))
		{
			/* se->type is volatile as SSR allocation */
			type = __f2fs_get_curseg(segno);
			f2fs_bug_on(this, type == NO_CHECK_TYPE);
		}
		else {type = CURSEG_WARM_DATA;}
	}

	f2fs_bug_on(this, !IS_DATASEG(type));
	curseg = this->CURSEG_I( type);

	mutex_lock(&curseg->curseg_mutex);
	down_write(&sit_i->sentry_lock);

	old_cursegno = curseg->segno;
	old_blkoff = curseg->next_blkoff;
	old_alloc_type = curseg->alloc_type;

	/* change the current segment */
	if (segno != curseg->segno) {
		curseg->next_segno = segno;
		change_curseg(this, type, true);
	}

	curseg->next_blkoff = GET_BLKOFF_FROM_SEG0(this, new_blkaddr);
	__add_sum_entry(this, type, sum);

	if (!recover_curseg || recover_newaddr) {
		if (!from_gc)
			update_segment_mtime(this, new_blkaddr, 0);
		update_sit_entry(this, new_blkaddr, 1);
	}
	if (GET_SEGNO(this, old_blkaddr) != NULL_SEGNO) {
		invalidate_mapping_pages(META_MAPPING(this),
					old_blkaddr, old_blkaddr);
		if (!from_gc)
			update_segment_mtime(this, old_blkaddr, 0);
		update_sit_entry(this, old_blkaddr, -1);
	}

	locate_dirty_segment(this, GET_SEGNO(this, old_blkaddr));
	locate_dirty_segment(this, GET_SEGNO(this, new_blkaddr));

	locate_dirty_segment(this, old_cursegno);

	if (recover_curseg) {
		if (old_cursegno != curseg->segno) {
			curseg->next_segno = old_cursegno;
			change_curseg(this, type, true);
		}
		curseg->next_blkoff = old_blkoff;
		curseg->alloc_type = old_alloc_type;
	}

	up_write(&sit_i->sentry_lock);
	mutex_unlock(&curseg->curseg_mutex);
	up_write(&this->SM_I()->curseg_lock);
}

void f2fs_replace_block(f2fs_sb_info *sbi, dnode_of_data *dn,
				block_t old_addr, block_t new_addr,
				unsigned char version, bool recover_curseg,
				bool recover_newaddr)
{
	struct f2fs_summary sum;

	set_summary(&sum, dn->nid, dn->ofs_in_node, version);

	sbi->f2fs_do_replace_block(&sum, old_addr, new_addr, recover_curseg, recover_newaddr, false);

	f2fs_update_data_blkaddr(dn, new_addr);
}

void f2fs_wait_on_page_writeback(struct page *page, enum page_type type, bool ordered, bool locked)
{
	if (PageWriteback(page)) {
		struct f2fs_sb_info *sbi = F2FS_P_SB(page);

		/* submit cached LFS IO */
		f2fs_submit_merged_write_cond(sbi, NULL, page, 0, type);
		/* sbumit cached IPU IO */
		f2fs_submit_merged_ipu_write(sbi, NULL, page);
		if (ordered)
		{
			wait_on_page_writeback(page);
			f2fs_bug_on(sbi, locked && PageWriteback(page));
		}
		else
		{
			wait_for_stable_page(page);
		}
	}
}


void f2fs_wait_on_block_writeback(f2fs_inode_info *inode, block_t blkaddr)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct page *cpage;

	if (!f2fs_post_read_required(inode))
		return;

	if (!__is_valid_data_blkaddr(blkaddr))
		return;

	cpage = find_lock_page(META_MAPPING(sbi), blkaddr);
	if (cpage) {
		f2fs_wait_on_page_writeback(cpage, DATA, true, true);
		f2fs_put_page(cpage, 1);
	}
}

void f2fs_wait_on_block_writeback_range(f2fs_inode_info*inode, block_t blkaddr, block_t len)
{
	block_t i;

	for (i = 0; i < len; i++)	f2fs_wait_on_block_writeback(inode, blkaddr + i);
}

// 从磁盘读取 current segment info
//static int read_compacted_summaries(struct f2fs_sb_info *sbi)
int f2fs_sb_info::read_compacted_summaries(void)
{
	f2fs_checkpoint *ckpt = F2FS_CKPT();
	curseg_info *seg_i;
	unsigned char *kaddr;
	struct page *ppage;
	block_t start;
	int i, j, offset;

	start = start_sum_block();

	ppage = f2fs_get_meta_page(start++);
	if (IS_ERR(ppage))		return (int)PTR_ERR(ppage);
	kaddr = page_address<unsigned char>(ppage);

#ifdef _DEBUG
	jcvos::Utf8ToUnicode(ppage->m_type, "seg");
	LOG_DEBUG(L"new page: page=%p, addr=%p, type=%s, index=%d", ppage, ppage->virtual_add, ppage->m_type.c_str(), ppage->index);
#endif

	/* Step 1: restore nat cache */
	seg_i = CURSEG_I(CURSEG_HOT_DATA);
	memcpy_s(&seg_i->journal, sizeof(f2fs_journal), kaddr, SUM_JOURNAL_SIZE);

	/* Step 2: restore sit cache */
	seg_i = CURSEG_I(CURSEG_COLD_DATA);
	memcpy_s(&seg_i->journal, sizeof(f2fs_journal), kaddr + SUM_JOURNAL_SIZE, SUM_JOURNAL_SIZE);
	offset = 2 * SUM_JOURNAL_SIZE;

	/* Step 3: restore summary entries */
	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++)
	{
		unsigned short blk_off;
		unsigned int segno;

		seg_i = CURSEG_I(i);
		segno = le32_to_cpu(ckpt->cur_data_segno[i]);
		blk_off = le16_to_cpu(ckpt->cur_data_blkoff[i]);
		seg_i->next_segno = segno;
		reset_curseg(this, i, 0);
		seg_i->alloc_type = ckpt->alloc_type[i];
		seg_i->next_blkoff = blk_off;

		if (seg_i->alloc_type == SSR)			blk_off = blocks_per_seg;

		for (j = 0; j < blk_off; j++) 
		{
			f2fs_summary *s;
			s = (f2fs_summary *)(kaddr + offset);
			seg_i->sum_blk.entries[j] = *s;
			offset += SUMMARY_SIZE;
			if (offset + SUMMARY_SIZE <= PAGE_SIZE -
						SUM_FOOTER_SIZE)
				continue;

			f2fs_put_page(ppage, 1);
			ppage = NULL;

			ppage = f2fs_get_meta_page(start++);
			if (IS_ERR(ppage)) 				return (int)PTR_ERR(ppage);
			kaddr = page_address<unsigned char>(ppage);
			offset = 0;
		}
	}
	f2fs_put_page(ppage, 1);
	return 0;
}


//static int read_normal_summaries(f2fs_sb_info *sbi, int type)
int f2fs_sb_info::read_normal_summaries(int type)
{
	f2fs_checkpoint *ckpt = F2FS_CKPT();
	f2fs_summary_block *sum;
	curseg_info *curseg;
	page *new_page;
	unsigned short blk_off;
	unsigned int segno = 0;
	block_t blk_addr = 0;
	int err = 0;

	/* get segment number and block addr */
	if (IS_DATASEG(type)) {
		segno = le32_to_cpu(ckpt->cur_data_segno[type]);
		blk_off = le16_to_cpu(ckpt->cur_data_blkoff[type -
							CURSEG_HOT_DATA]);
		if (__exist_node_summaries())		blk_addr = sum_blk_addr(NR_CURSEG_PERSIST_TYPE, type);
		else								blk_addr = sum_blk_addr(NR_CURSEG_DATA_TYPE, type);
	} else {
		segno = le32_to_cpu(ckpt->cur_node_segno[type -
							CURSEG_HOT_NODE]);
		blk_off = le16_to_cpu(ckpt->cur_node_blkoff[type -
							CURSEG_HOT_NODE]);
		if (__exist_node_summaries())		blk_addr = sum_blk_addr( NR_CURSEG_NODE_TYPE,	type - CURSEG_HOT_NODE);
		else								blk_addr = GET_SUM_BLOCK(this, segno);
	}

	new_page = f2fs_get_meta_page(blk_addr);
	if (IS_ERR(new_page))	return (int)PTR_ERR(new_page);
	sum = page_address<f2fs_summary_block>(new_page);

	if (IS_NODESEG(type)) 
	{
		if (__exist_node_summaries())
		{
			f2fs_summary *ns = &sum->entries[0];
			for (UINT i = 0; i < blocks_per_seg; i++, ns++)
			{
				ns->_u._s.version = 0;
				ns->_u._s.ofs_in_node = 0;
			}
		} 
		else 
		{
			err = f2fs_restore_node_summary(this, segno, sum);
			if (err)				goto out;
		}
	}

	/* set uncompleted segment to curseg */
	curseg = CURSEG_I(type);
	mutex_lock(&curseg->curseg_mutex);

	/* update journal info */
	down_write(&curseg->journal_rwsem);
	memcpy_s(&curseg->journal, sizeof(f2fs_journal), &sum->journal, SUM_JOURNAL_SIZE);
	up_write(&curseg->journal_rwsem);

	memcpy_s(curseg->sum_blk.entries, sizeof(curseg->sum_blk.entries), sum->entries, SUM_ENTRY_SIZE);
	memcpy_s(&curseg->sum_blk.footer, sizeof(curseg->sum_blk.footer), &sum->footer, SUM_FOOTER_SIZE);
	curseg->next_segno = segno;
	reset_curseg(this, type, 0);
	curseg->alloc_type = ckpt->alloc_type[type];
	curseg->next_blkoff = blk_off;
	mutex_unlock(&curseg->curseg_mutex);
out:
	f2fs_put_page(new_page, 1);
	return err;
}



int f2fs_sb_info::restore_curseg_summaries(void)
{
	f2fs_journal *sit_j = &CURSEG_I(CURSEG_COLD_DATA)->journal;
	f2fs_journal *nat_j = &CURSEG_I(CURSEG_HOT_DATA)->journal;
	int type = CURSEG_HOT_DATA;
	int err;

	if (is_set_ckpt_flags(CP_COMPACT_SUM_FLAG))
	{
		int npages = f2fs_npages_for_summary_flush(true);
		if (npages >= 2)	f2fs_ra_meta_pages(start_sum_block(), npages, META_CP, true);
		/* restore for compacted data summary */
		err = read_compacted_summaries();
		if (err)			return err;
		type = CURSEG_HOT_NODE;
	}

	if (__exist_node_summaries())
		f2fs_ra_meta_pages(sum_blk_addr(NR_CURSEG_PERSIST_TYPE, type), NR_CURSEG_PERSIST_TYPE - type, META_CP, true);

	for (; type <= CURSEG_COLD_NODE; type++) 
	{
		err = read_normal_summaries(type);
		if (err) return err;
	}

	/* sanity check for summary blocks */
	if (nats_in_cursum(nat_j) > NAT_JOURNAL_ENTRIES ||	sits_in_cursum(sit_j) > SIT_JOURNAL_ENTRIES)
	{
		f2fs_err(sbi, L"invalid journal entries nats %u sits %u", nats_in_cursum(nat_j), sits_in_cursum(sit_j));
		return -EINVAL;
	}
	return 0;
}

static void write_compacted_summaries(struct f2fs_sb_info *sbi, block_t blkaddr)
{
	struct page *page;
	unsigned char *kaddr;
	struct f2fs_summary *summary;
	struct curseg_info *seg_i;
	int written_size = 0;
	int i, j;

	page = f2fs_grab_meta_page(sbi, blkaddr++);
	kaddr = page_address<unsigned char>(page);
	memset(kaddr, 0, PAGE_SIZE);

	/* Step 1: write nat cache */
	seg_i = sbi->CURSEG_I( CURSEG_HOT_DATA);
	memcpy(kaddr, &seg_i->journal, SUM_JOURNAL_SIZE);
	written_size += SUM_JOURNAL_SIZE;

	/* Step 2: write sit cache */
	seg_i = sbi->CURSEG_I( CURSEG_COLD_DATA);
	memcpy(kaddr + written_size, &seg_i->journal, SUM_JOURNAL_SIZE);
	written_size += SUM_JOURNAL_SIZE;

	/* Step 3: write summary entries */
	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++) {
		unsigned short blkoff;

		seg_i = sbi->CURSEG_I( i);
		if (sbi->ckpt->alloc_type[i] == SSR)
			blkoff = sbi->blocks_per_seg;
		else
			blkoff = sbi->curseg_blkoff(i);

		for (j = 0; j < blkoff; j++) {
			if (!page) {
				page = f2fs_grab_meta_page(sbi, blkaddr++);
				kaddr = page_address<unsigned char>(page);
				memset(kaddr, 0, PAGE_SIZE);
				written_size = 0;
			}
			summary = (f2fs_summary *)(kaddr + written_size);
			*summary = seg_i->sum_blk.entries[j];
			written_size += SUMMARY_SIZE;

			if (written_size + SUMMARY_SIZE <= PAGE_SIZE - SUM_FOOTER_SIZE)
				continue;

			set_page_dirty(page);
			f2fs_put_page(page, 1);
			page = NULL;
		}
	}
	if (page) {
		set_page_dirty(page);
		f2fs_put_page(page, 1);
	}
}

static void write_normal_summaries(struct f2fs_sb_info *sbi,
					block_t blkaddr, int type)
{
	int i, end;

	if (IS_DATASEG(type))
		end = type + NR_CURSEG_DATA_TYPE;
	else
		end = type + NR_CURSEG_NODE_TYPE;

	for (i = type; i < end; i++)
		write_current_sum_page(sbi, i, blkaddr + (i - type));
}

void f2fs_write_data_summaries(struct f2fs_sb_info *sbi, block_t start_blk)
{
	if (sbi->is_set_ckpt_flags( CP_COMPACT_SUM_FLAG))
		write_compacted_summaries(sbi, start_blk);
	else
		write_normal_summaries(sbi, start_blk, CURSEG_HOT_DATA);
}

void f2fs_write_node_summaries(f2fs_sb_info *sbi, block_t start_blk)
{
	write_normal_summaries(sbi, start_blk, CURSEG_HOT_NODE);
}

int f2fs_lookup_journal_in_cursum(f2fs_journal *journal, int type, unsigned int val, int alloc)
{
	int i;

	if (type == NAT_JOURNAL) 
	{
		for (i = 0; i < nats_in_cursum(journal); i++) 
		{
			if (le32_to_cpu(nid_in_journal(journal, i)) == val)			return i;
		}
		if (alloc && __has_cursum_space(journal, 1, NAT_JOURNAL))
			return update_nats_in_cursum(journal, 1);
	} 
	else if (type == SIT_JOURNAL)
	{
		for (i = 0; i < sits_in_cursum(journal); i++)
		{
			if (le32_to_cpu(segno_in_journal(journal, i)) == val)
			{
				LOG_DEBUG(L"sit_entry has already in journal, segment no=%d, offset=%d", val, i);
				return i;
			}
		}
		if (alloc && __has_cursum_space(journal, 1, SIT_JOURNAL))
		{
			LOG_DEBUG(L"allocate journal");
			return update_sits_in_cursum(journal, 1);
		}
	}
	return -1;
}


//static struct page *get_current_sit_page(struct f2fs_sb_info *sbi, unsigned int segno)
page * f2fs_sb_info::get_current_sit_page( unsigned int segno)
{
	pgoff_t addr = current_sit_addr(segno);
	LOG_DEBUG_(1, L"current segment address, seg no=%d, block=0x%X", segno, addr);
	return f2fs_get_meta_page(addr);
}

//static struct page *get_next_sit_page(struct f2fs_sb_info *sbi,	unsigned int start)
page* f2fs_sb_info::get_next_sit_page(unsigned int start)
{
	sit_info *sit_i = SIT_I();
	page *ppage;
	pgoff_t src_off, dst_off;

	src_off = current_sit_addr(start);
	dst_off = next_sit_addr(this, src_off);

	ppage = f2fs_grab_meta_page(this, dst_off);
	seg_info_to_sit_page(this, ppage, start);

	set_page_dirty(ppage);
	set_to_next_sit(sit_i, start);

	return ppage;
}

static struct sit_entry_set *grab_sit_entry_set(void)
{
	sit_entry_set *ses = f2fs_kmem_cache_alloc<sit_entry_set>(/*sit_entry_set_slab*/NULL, GFP_NOFS);

	ses->entry_cnt = 0;
	INIT_LIST_HEAD(&ses->set_list);
	return ses;
}

static void release_sit_entry_set(struct sit_entry_set *ses)
{
	list_del(&ses->set_list);
	kmem_cache_free(/*sit_entry_set_slab*/NULL, ses);
}

static void adjust_sit_entry_set(sit_entry_set *ses,list_head *head)
{
	struct sit_entry_set *next = ses;

	if (list_is_last(&ses->set_list, head))
		return;

	list_for_each_entry_continue(sit_entry_set, next, head, set_list)
		if (ses->entry_cnt <= next->entry_cnt)
			break;

	list_move_tail(&ses->set_list, &next->set_list);
}

static void add_sit_entry(unsigned int segno, struct list_head *head)
{
	struct sit_entry_set *ses;
	unsigned int start_segno = START_SEGNO(segno);

	list_for_each_entry(sit_entry_set, ses, head, set_list) 
	{
		if (ses->start_segno == start_segno) {
			ses->entry_cnt++;
			adjust_sit_entry_set(ses, head);
			return;
		}
	}

	ses = grab_sit_entry_set();

	ses->start_segno = start_segno;
	ses->entry_cnt++;
	list_add(&ses->set_list, head);
}


void f2fs_sb_info::add_sits_in_set(void)
{
	f2fs_sm_info *sm_info = SM_I();
	list_head *set_list = &sm_info->sit_entry_set;
	unsigned long *bitmap = SIT_I()->dirty_sentries_bitmap;
	unsigned int segno;
	for_each_set_bit(segno, bitmap, MAIN_SEGS())
	{
		LOG_DEBUG(L"add dirty sentry, segment=%d", segno);
		add_sit_entry(segno, set_list);
	}
}

void f2fs_sb_info::remove_sits_in_journal(void)
{
	//LOG_STACK_TRACE();
	curseg_info *curseg = CURSEG_I( CURSEG_COLD_DATA);
	f2fs_journal *journal = &curseg->journal;
	int i;

	down_write(&curseg->journal_rwsem);
	for (i = 0; i < sits_in_cursum(journal); i++) 
	{
		unsigned int segno;
		bool dirtied;

		segno = le32_to_cpu(segno_in_journal(journal, i));
		dirtied = sm_info->sit_info->__mark_sit_entry_dirty(segno);

		if (!dirtied)
			add_sit_entry(segno, &SM_I()->sit_entry_set);
		LOG_DEBUG(L"remove sit_entry from journal, segment no=%d, dirtied=%d", segno, dirtied);
	}
	update_sits_in_cursum(journal, -i);
	up_write(&curseg->journal_rwsem);
}

/* CP calls this function, which flushes SIT entries including sit_journal, and moves prefree segs to free segs.*/
//void f2fs_flush_sit_entries(f2fs_sb_info *sbi, cp_control *cpc)
void f2fs_sb_info::f2fs_flush_sit_entries(cp_control* cpc)
{
	sit_info *sit_i = this->SIT_I();
	unsigned long *bitmap = sit_i->dirty_sentries_bitmap;
	curseg_info *curseg = CURSEG_I( CURSEG_COLD_DATA);
	f2fs_journal *journal = &curseg->journal;
	sit_entry_set *ses, *tmp;
	list_head *head = &SM_I()->sit_entry_set;
	bool to_journal = !is_sbi_flag_set( SBI_IS_RESIZEFS);
	seg_entry *se;

	down_write(&sit_i->sentry_lock);

	if (!sit_i->dirty_sentries)		goto out;

	/* add and account sit entries of dirty bitmap in sit entry set temporarily	 */
	add_sits_in_set();

	LOG_DEBUG(L"block_size=%d, sum_foot=%d, entries_size=%d, journal_size=%d", F2FS_BLKSIZE, SUM_FOOTER_SIZE, SUM_ENTRIES_SIZE, SUM_JOURNAL_SIZE);
	LOG_DEBUG(L"total sit journal entries=%d, sit_journal_entry size=%d", F2FS_BLKSIZE, SIT_JOURNAL_ENTRIES, sizeof(sit_journal_entry));
	/* if there are no enough space in journal to store dirty sit entries, remove all entries from journal and add and account them in sit entry set. */
	LOG_DEBUG(L"sit journal: used=%d, remain=%d, dirty entries=%d", sits_in_cursum(journal), MAX_SIT_JENTRIES(journal), sit_i->dirty_sentries);
	if (!__has_cursum_space(journal, sit_i->dirty_sentries, SIT_JOURNAL) ||	!to_journal)
		remove_sits_in_journal();

	/* there are two steps to flush sit entries:
	 * #1, flush sit entries to journal in current cold data summary block.
	 * #2, flush sit entries to sit page.	 */
	list_for_each_entry_safe(sit_entry_set, ses, tmp, head, set_list) 
	{
		page *ppage = NULL;
		f2fs_sit_block *raw_sit = NULL;
		unsigned int start_segno = ses->start_segno;
		unsigned int end = min(start_segno + SIT_ENTRY_PER_BLOCK, (unsigned long)MAIN_SEGS());
		unsigned int segno = start_segno;

		LOG_DEBUG(L"sit journal: used=%d, remain=%d, dirty entries=%d", sits_in_cursum(journal), MAX_SIT_JENTRIES(journal), sit_i->dirty_sentries);
		if (to_journal && !__has_cursum_space(journal, ses->entry_cnt, SIT_JOURNAL))
			to_journal = false;
		LOG_DEBUG(L"update sit to <%s>", to_journal ? L"journal" : L"sit");

		if (to_journal) {	down_write(&curseg->journal_rwsem);	}
		else
		{
			ppage = get_next_sit_page(start_segno);
			raw_sit = page_address<f2fs_sit_block>(ppage);
			LOG_DEBUG(L"get sit page, page=%p, index=0x%X, segment_no=%d", ppage, ppage->index, start_segno);
		}

		/* flush dirty sit entries in region of current sit set */
		for_each_set_bit_from(segno, bitmap, end) 
		{
			int offset, sit_offset;
			se = get_seg_entry(segno);
#ifdef CONFIG_F2FS_CHECK_FS
			if (memcmp(se->cur_valid_map, se->cur_valid_map_mir, SIT_VBLOCK_MAP_SIZE))
				f2fs_bug_on(this, 1);
#endif
			/* add discard candidates */
			if (!(cpc->reason & CP_DISCARD)) 
			{
				cpc->trim_start = segno;
				add_discard_addrs(this, cpc, false);
			}
			LOG_DEBUG(L"update segment, seg_no=%d, type=%d, valid_blocks=%d", segno, se->type, se->valid_blocks);
			if (to_journal) 
			{
				offset = f2fs_lookup_journal_in_cursum(journal,	SIT_JOURNAL, segno, 1);
				f2fs_bug_on(this, offset < 0);
				segno_in_journal(journal, offset) =	cpu_to_le32(segno);
				seg_info_to_raw_sit(se,	&sit_in_journal(journal, offset));
				check_block_count( segno, &sit_in_journal(journal, offset));
			} 
			else
			{
				sit_offset = SIT_ENTRY_OFFSET(sit_i, segno);
				seg_info_to_raw_sit(se, &raw_sit->entries[sit_offset]);
				check_block_count( segno, &raw_sit->entries[sit_offset]);
			}

			__clear_bit(segno, bitmap);
			sit_i->dirty_sentries--;
			ses->entry_cnt--;
		}

		if (to_journal)		up_write(&curseg->journal_rwsem);
		else				f2fs_put_page(ppage, 1);

		f2fs_bug_on(this, ses->entry_cnt);
		release_sit_entry_set(ses);
	}

	f2fs_bug_on(this, !::list_empty(head));
	f2fs_bug_on(this, sit_i->dirty_sentries);
out:
	if (cpc->reason & CP_DISCARD) 
	{
		__u64 trim_start = cpc->trim_start;
		for (; cpc->trim_start <= cpc->trim_end; cpc->trim_start++)
			add_discard_addrs(this, cpc, false);
		cpc->trim_start = trim_start;
	}
	up_write(&sit_i->sentry_lock);
	set_prefree_as_free_segments(this);
}

int f2fs_sb_info::build_sit_info(void)
{
	f2fs_super_block *raw_super = F2FS_RAW_SUPER();
	unsigned int sit_segs, start;
	//char  *bitmap;
	unsigned int bitmap_size, main_bitmap_size, sit_bitmap_size;

	/* allocate memory for SIT information */
	//sit_i = f2fs_kzalloc(sbi, sizeof(struct sit_info), GFP_KERNEL);
	// sit_info是虚拟类，必须使用new创建，不能直接清0；
	sit_info* sit_i = new sit_info;
	if (!sit_i)	THROW_ERROR(ERR_MEM, L"failed on creating sit_info");

	sm_info->sit_info = sit_i;

	//f2fs_kvzalloc(sbi, array_size(sizeof(struct seg_entry), sbi->MAIN_SEGS()),    GFP_KERNEL);
	sit_i->sentries = new_and_zero_array<seg_entry>(MAIN_SEGS());

	main_bitmap_size = f2fs_bitmap_size(MAIN_SEGS());
//	sit_i->dirty_sentries_bitmap = f2fs_kvzalloc(sbi, main_bitmap_size, GFP_KERNEL);
	sit_i->dirty_sentries_bitmap = f2fs_kvzalloc<unsigned long>(NULL, main_bitmap_size/*, GFP_KERNEL*/);

#ifdef CONFIG_F2FS_CHECK_FS
	bitmap_size = sbi->MAIN_SEGS() * SIT_VBLOCK_MAP_SIZE * 4;
#else
	bitmap_size = MAIN_SEGS() * SIT_VBLOCK_MAP_SIZE * 3;
#endif
	sit_i->bitmap = f2fs_kvzalloc<BYTE>(NULL, bitmap_size/*, GFP_KERNEL*/);
	BYTE * bitmap = sit_i->bitmap;

	for (start = 0; start < MAIN_SEGS(); start++)
	{
		sit_i->sentries[start].cur_valid_map = bitmap;
		bitmap += SIT_VBLOCK_MAP_SIZE;

		sit_i->sentries[start].ckpt_valid_map = bitmap;
		bitmap += SIT_VBLOCK_MAP_SIZE;

#ifdef CONFIG_F2FS_CHECK_FS
		sit_i->sentries[start].cur_valid_map_mir = bitmap;
		bitmap += SIT_VBLOCK_MAP_SIZE;
#endif
		sit_i->sentries[start].discard_map = bitmap;
		bitmap += SIT_VBLOCK_MAP_SIZE;
	}

//	sit_i->tmp_map = f2fs_kzalloc(sbi, SIT_VBLOCK_MAP_SIZE, GFP_KERNEL);
	sit_i->tmp_map = f2fs_kzalloc<unsigned long>(NULL, SIT_VBLOCK_MAP_SIZE/*, GFP_KERNEL*/);

	if (__is_large_section()) 
	{
//		sit_i->sec_entries = f2fs_kvzalloc(sbi, array_size(sizeof(struct sec_entry), sbi->MAIN_SECS()), GFP_KERNEL);
		sit_i->sec_entries = f2fs_kvzalloc<sec_entry>(NULL, MAIN_SECS()/*, GFP_KERNEL*/);	//<PAIR> f2fs_sm_info::destroy_sit_info中删除
	}

	/* get information related with SIT */
	sit_segs = le32_to_cpu(raw_super->segment_count_sit) >> 1;

	/* setup SIT bitmap from ckeckpoint pack */
	sit_bitmap_size = __bitmap_size(SIT_BITMAP);
	BYTE * src_bitmap = __bitmap_ptr(SIT_BITMAP);

	sit_i->sit_bitmap = kmemdup<BYTE>(src_bitmap, sit_bitmap_size, GFP_KERNEL);
	if (!sit_i->sit_bitmap)	return -ENOMEM;

#ifdef CONFIG_F2FS_CHECK_FS
	sit_i->sit_bitmap_mir = kmemdup(src_bitmap,	sit_bitmap_size, GFP_KERNEL);
	if (!sit_i->sit_bitmap_mir)	return -ENOMEM;

	sit_i->invalid_segmap = f2fs_kvzalloc(sbi,	main_bitmap_size, GFP_KERNEL);
	if (!sit_i->invalid_segmap)	return -ENOMEM;
#endif

	/* init SIT information */
//	sit_i->s_ops = &default_salloc_ops;
	//sit_i->s_ops.allocate_segment = allocate_segment_by_default;

	sit_i->sit_base_addr = le32_to_cpu(raw_super->sit_blkaddr);
	sit_i->sit_blocks = sit_segs << log_blocks_per_seg;
	sit_i->written_valid_blocks = 0;
	sit_i->bitmap_size = sit_bitmap_size;
	sit_i->dirty_sentries = 0;
	sit_i->sents_per_block = SIT_ENTRY_PER_BLOCK;
	sit_i->elapsed_time = le64_to_cpu(ckpt->elapsed_time);
	sit_i->mounted_time = jcvos::GetTimeStamp();	//ktime_get_boottime_seconds();
	init_rwsem(&sit_i->sentry_lock);
	return 0;
}


int f2fs_sb_info::build_free_segmap(void)
{
	//struct free_segmap_info *free_i;
	unsigned int bitmap_size, sec_bitmap_size;

	/* allocate memory for free segmap information */
//	free_i = f2fs_kzalloc(sbi, sizeof(struct free_segmap_info), GFP_KERNEL);
	free_segmap_info* free_i = new free_segmap_info; // f2fs_kzalloc<free_segmap_info>(NULL, 1);
	if (!free_i)	return -ENOMEM;
	sm_info->free_info = free_i;
	bitmap_size = f2fs_bitmap_size(MAIN_SEGS());
//	free_i->free_segmap = f2fs_kvmalloc(sbi, bitmap_size, GFP_KERNEL);
	free_i->free_segmap = new unsigned long[bitmap_size];//f2fs_kvmalloc<unsigned long>(NULL, bitmap_size);
	if (!free_i->free_segmap)		return -ENOMEM;

	sec_bitmap_size = f2fs_bitmap_size(MAIN_SECS());
	//free_i->free_secmap = f2fs_kvmalloc(sbi, sec_bitmap_size, GFP_KERNEL);
	free_i->free_secmap = new unsigned long[sec_bitmap_size];//f2fs_kvmalloc<unsigned long>(NULL, sec_bitmap_size);
	if (!free_i->free_secmap)		return -ENOMEM;

	/* set all segments as dirty temporarily */
	memset(free_i->free_segmap, 0xff, bitmap_size*sizeof(unsigned long));
	memset(free_i->free_secmap, 0xff, sec_bitmap_size*sizeof(unsigned long));

	/* init free segmap information */
	//block_t blk_addr = (sm_info ? sm_info->main_blkaddr : le32_to_cpu(raw_super->main_blkaddr));
	//block_t seg0_blkaddr = (sm_info) ? sm_info->seg0_blkaddr : le32_to_cpu(raw_super->segment0_blkaddr);
	//free_i->start_segno = (((blk_addr)-seg0_blkaddr) >> log_blocks_per_seg);

	free_i->start_segno = GET_SEGNO_FROM_SEG0(this, MAIN_BLKADDR(this));
	free_i->free_segments = 0;
	free_i->free_sections = 0;
	spin_lock_init(&free_i->segmap_lock);
	return 0;
}

//int f2fs_sb_info::build_curseg(void)
int f2fs_sm_info::build_curseg(f2fs_sb_info* sbi)
{
	//struct curseg_info *array;
	int i;

	//array = f2fs_kzalloc(sbi, array_size(NR_CURSEG_TYPE, sizeof(*array)), GFP_KERNEL);
	curseg_info* array_buf = new curseg_info[NR_CURSEG_TYPE]; // f2fs_kzalloc<curseg_info>(NULL, NR_CURSEG_TYPE);
	if (!array_buf)	return -ENOMEM;
	curseg_array = array_buf;

	for (i = 0; i < NO_CHECK_TYPE; i++) 
	{
		mutex_init(&array_buf[i].curseg_mutex);
//		array_buf[i].sum_blk = f2fs_kzalloc(sbi, PAGE_SIZE, GFP_KERNEL);
		LOG_DEBUG(L"allocal sum_blk, size=%lld", sizeof(f2fs_summary_block));
//		array_buf[i].sum_blk = new f2fs_summary_block; //f2fs_kzalloc<f2fs_summary_block>(NULL, 1);	// need to adjust size
//		if (!array_buf[i].sum_blk)		return -ENOMEM;
		memset(&array_buf[i].sum_blk, 0, sizeof(f2fs_summary_block));

		init_rwsem(&array_buf[i].journal_rwsem);
//		array_buf[i].journal = f2fs_kzalloc(sbi, sizeof(struct f2fs_journal), GFP_KERNEL);
//		array_buf[i].journal = new f2fs_journal; //f2fs_kzalloc<f2fs_journal>(NULL, 1);
//		if (!array_buf[i].journal)		return -ENOMEM;
		memset(&array_buf[i].journal, 0, sizeof(f2fs_journal));

		if (i < NR_PERSISTENT_LOG)				array_buf[i].seg_type = CURSEG_HOT_DATA + i;
		else if (i == CURSEG_COLD_DATA_PINNED)	array_buf[i].seg_type = CURSEG_COLD_DATA;
		else if (i == CURSEG_ALL_DATA_ATGC)		array_buf[i].seg_type = CURSEG_COLD_DATA;
		array_buf[i].segno = NULL_SEGNO;
		array_buf[i].next_blkoff = 0;
		array_buf[i].inited = false;
	}
	return sbi->restore_curseg_summaries();
}


int f2fs_sb_info::build_sit_entries(void)
{
	sit_info *sit_i = SIT_I();
	curseg_info *curseg = CURSEG_I(CURSEG_COLD_DATA);
	seg_entry *se;
//	f2fs_sit_entry sit;
	// sit entry / sit entry per block
	UINT sit_blk_cnt = SIT_BLK_CNT(this);
	LOG_DEBUG(L"main segs=%d, sit size=%zd, sit per blk=%d, sit blk cnt=%d", MAIN_SEGS(), sizeof(f2fs_sit_entry),
		SIT_ENTRY_PER_BLOCK, sit_blk_cnt);
	unsigned int i;
	unsigned int readed, start_blk = 0;
	int err = 0;
	block_t total_node_blocks = 0;

	// 第一步：从SIT block中读取current valid bitmap以及ckpt valid map
	do 
	{
		//<YUAN> 查看read ahead以后，page存放在何处？后续的get_current_sit_page()是否应该从缓存中读取。
		// => read ahead读取的page放在缓存中。缓存以inode->mapping->page的结构保存。
		// => 下次获取page时，如果已经读取，则直接从cache中取。参考分get_current_sit_page()
		readed = f2fs_ra_meta_pages(start_blk, BIO_MAX_VECS, META_SIT, true);

		unsigned int start = start_blk * sit_i->sents_per_block;
		unsigned int  end = (start_blk + readed) * sit_i->sents_per_block;
		LOG_DEBUG_(1, L"check segment from=%d, to=%d or %d", start, end, MAIN_SECS());

		for (; start < end && start < MAIN_SEGS(); start++)
		{
			// 读取基本的sit block。后面会从summary中覆盖修改的部分。
			se = &sit_i->sentries[start];
			page * ppage = get_current_sit_page(start);
// 当初为了解决下方put_page()的问题增加的处理。后来判明，应当在pagecache_get_page()中进行计数。重新修复错误。
//			atomic_inc(&ppage->_refcount);	
			if (IS_ERR(ppage))				return (int)PTR_ERR(ppage);
			f2fs_sit_block * sit_blk = page_address<f2fs_sit_block>(ppage);
			f2fs_sit_entry& sit = sit_blk->entries[SIT_ENTRY_OFFSET(sit_i, start)];
			LOG_DEBUG_(1, L"read sit block, page=%p, index=0x%X, seg no=%d", ppage, ppage->index, start);
			LOG_DEBUG_(1, L"seg no=%d, type=%d, valid blocks=%d", start, 
				(sit_blk->entries[start].vblocks >>10), (sit_blk->entries[start].vblocks & 0x3FF));
			f2fs_put_page(ppage, 1);

			err = check_block_count(start, &sit);
			if (err)
			{
				LOG_ERROR(L"[err] failed on checking block count in step #1, seg no=%d, err=%d", start, err);
				return err;
			}
			seg_info_from_raw_sit(se, &sit);
			if (IS_NODESEG(se->type))
			{
				LOG_DEBUG_(1, L"count node block, seg_no=%d, +%d, total_node_block=%d", start, se->valid_blocks, total_node_blocks);
				total_node_blocks += se->valid_blocks;
			}

			/* build discard map only one time */
			if (is_set_ckpt_flags(CP_TRIMMED_FLAG)) 	{	memset(se->discard_map, 0xff,	SIT_VBLOCK_MAP_SIZE);	} 
			else 
			{
				memcpy_s(se->discard_map,	SIT_VBLOCK_MAP_SIZE, se->cur_valid_map, SIT_VBLOCK_MAP_SIZE);
				discard_blks += blocks_per_seg - se->valid_blocks;
			}
			if (__is_large_section())	get_sec_entry(this, start)->valid_blocks +=se->valid_blocks;
		}
		start_blk += readed;
	} while (start_blk < sit_blk_cnt);

	// 第二步：从summary中恢复差异部分。
	f2fs_journal *journal = & curseg->journal;
	down_read(&curseg->journal_rwsem);
	for (i = 0; i < sits_in_cursum(journal); i++) 	// 按journal的数量 journal->n_sits读取
	{
		unsigned int old_valid_blocks;

		unsigned int start = le32_to_cpu(segno_in_journal(journal, i));	//起始segment id
		if (start >= MAIN_SEGS()) 
		{
//			f2fs_err(this, L"Wrong journal entry on segno %u", start);
			err = -EFSCORRUPTED;
			LOG_ERROR(L"[err] wrong journal entry on segno %u, err=%d", start, err);
			break;
		}

		se = &sit_i->sentries[start];
		f2fs_sit_entry & sit = sit_in_journal(journal, i);

		old_valid_blocks = se->valid_blocks;
		if (IS_NODESEG(se->type))
		{
			LOG_DEBUG_(1, L"remove old node block, -%d, total_node_block=%d", old_valid_blocks, total_node_blocks);
			total_node_blocks -= old_valid_blocks;
		}

		err = check_block_count(start, &sit);
		if (err)
		{
			LOG_ERROR(L"[err] failed on checking block count in step #2, seg no=%d, err=%d", start, err);
			break;
		}
		seg_info_from_raw_sit(se, &sit);
		if (IS_NODESEG(se->type))
		{
			LOG_DEBUG_(1, L"count node block, +%d, total_node_block=%d", se->valid_blocks, total_node_blocks);
			total_node_blocks += se->valid_blocks;
		}

		if (is_set_ckpt_flags(CP_TRIMMED_FLAG))
		{
			memset(se->discard_map, 0xff, SIT_VBLOCK_MAP_SIZE);
		}
		else
		{
			memcpy_s(se->discard_map, SIT_VBLOCK_MAP_SIZE, se->cur_valid_map, SIT_VBLOCK_MAP_SIZE);
			discard_blks += old_valid_blocks;
			discard_blks -= se->valid_blocks;
		}

		if (__is_large_section())
		{
			get_sec_entry(this, start)->valid_blocks +=	se->valid_blocks;
			get_sec_entry(this, start)->valid_blocks -= old_valid_blocks;
		}
	}
	up_read(&curseg->journal_rwsem);

	if (!err && total_node_blocks != valid_node_count()) 
	{
//		f2fs_err(this, L"SIT is corrupted node# %u vs %u", total_node_blocks, valid_node_count());
		err = -EFSCORRUPTED;
		LOG_ERROR(L"[ERR] SIT is corrupted node# %u vs %u, err=%d", total_node_blocks, valid_node_count(), err);
	}
	return err;
}

void f2fs_sb_info::init_free_segmap(void)
{
	unsigned int start;
	int type;
	seg_entry *sentry;

	for (start = 0; start < MAIN_SEGS(); start++) 
	{
		if (f2fs_usable_blks_in_seg(this, start) == 0)			continue;
		sentry = get_seg_entry(start);
		LOG_DEBUG_(1, L"process free in segment %d, valid blk=%d", start, sentry->valid_blocks);
		if (!sentry->valid_blocks)		__set_free(start);
		else							SIT_I()->written_valid_blocks += sentry->valid_blocks;
		LOG_DEBUG_(1, L"free segments=%d, free sections=%d", sm_info->free_info->free_segments, sm_info->free_info->free_sections);
	}

	/* set use the current segments */
	for (type = CURSEG_HOT_DATA; type <= CURSEG_COLD_NODE; type++)
	{
		curseg_info *curseg_t = CURSEG_I(type);
		__set_test_and_inuse(curseg_t->segno);
	}
}

void f2fs_sb_info::__set_free(unsigned int segno)
{
	free_segmap_info* free_i = sm_info->free_info;
	unsigned int secno = GET_SEC_FROM_SEG(this, segno);
	unsigned int start_segno = GET_SEG_FROM_SEC(this, secno);
	unsigned int next;
	unsigned int usable_segs = f2fs_usable_segs_in_sec(this, segno);

	spin_lock(&free_i->segmap_lock);
	__clear_bit(segno, free_i->free_segmap);
	free_i->free_segments++;

	next = find_next_bit(free_i->free_segmap, start_segno + segs_per_sec, start_segno);
	LOG_DEBUG_(1, L"sec=%d, start_seg=%d, seg=%d, next=%d, usable_seg=%d", secno, start_segno, segno, next, usable_segs);
	if (next >= start_segno + usable_segs)
	{
		__clear_bit(secno, free_i->free_secmap);
		free_i->free_sections++;
	}
	spin_unlock(&free_i->segmap_lock);
}

static void init_dirty_segmap(struct f2fs_sb_info *sbi)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	struct free_segmap_info *free_i = sbi->FREE_I();
	unsigned int segno = 0, offset = 0, secno;
	block_t valid_blocks, usable_blks_in_seg;
	block_t blks_per_sec = BLKS_PER_SEC(sbi);

	while (1) 
	{
		/* find dirty segment based on free segmap */
		segno = free_i->find_next_inuse(sbi->MAIN_SEGS(), offset);
		if (segno >= sbi->MAIN_SEGS())			break;
		offset = segno + 1;
		valid_blocks = get_valid_blocks(sbi, segno, false);
		usable_blks_in_seg = f2fs_usable_blks_in_seg(sbi, segno);
		if (valid_blocks == usable_blks_in_seg || !valid_blocks)			continue;
		if (valid_blocks > usable_blks_in_seg) 
		{
			f2fs_bug_on(sbi, 1);
			continue;
		}
		mutex_lock(&dirty_i->seglist_lock);
		__locate_dirty_segment(sbi, segno, DIRTY);
		mutex_unlock(&dirty_i->seglist_lock);
	}

	if (!sbi->__is_large_section())		return;

	mutex_lock(&dirty_i->seglist_lock);
	for (segno = 0; segno < sbi->MAIN_SEGS(); segno += sbi->segs_per_sec) 
	{
		valid_blocks = get_valid_blocks(sbi, segno, true);
		secno = GET_SEC_FROM_SEG(sbi, segno);

		if (!valid_blocks || valid_blocks == blks_per_sec)		continue;
		if (IS_CURSEC(sbi, secno))								continue;
		__set_bit(secno, dirty_i->dirty_secmap);
	}
	mutex_unlock(&dirty_i->seglist_lock);
}

//int f2fs_sb_info::init_victim_secmap()
int f2fs_sm_info::init_victim_secmap(unsigned int bitmap_size)
{
//	dirty_seglist_info *dirty_i = DIRTY_I(this);
//	unsigned int bitmap_size = f2fs_bitmap_size(MAIN_SECS());

//	dirty_i->victim_secmap = f2fs_kvzalloc(sbi, bitmap_size, GFP_KERNEL);
	dirty_info->victim_secmap = f2fs_kvzalloc<unsigned long>(NULL, bitmap_size);
	//if (!dirty_info->victim_secmap)		return -ENOMEM;
	//memset(dirty_info->victim_secmap, 0, sizeof(unsigned long) * bitmap_size);
	return 0;
}

int f2fs_sm_info::build_dirty_segmap(f2fs_sb_info* sbi)
{
	unsigned int bitmap_size, i;

	/* allocate memory for dirty segments list information */
//	dirty_i = f2fs_kzalloc(sbi, sizeof(struct dirty_seglist_info),	GFP_KERNEL);
	dirty_seglist_info* dirty_i = new dirty_seglist_info; 
	if (!dirty_i)		return -ENOMEM;

	dirty_info = dirty_i;
	mutex_init(&dirty_i->seglist_lock);
	bitmap_size = f2fs_bitmap_size(main_segments);

	for (i = 0; i < NR_DIRTY_TYPE; i++) 
	{
		dirty_i->dirty_segmap[i] = f2fs_kvzalloc<unsigned long>(sbi, bitmap_size/*,	GFP_KERNEL*/);	//<PAIR>f2fs_sm_info::discard_dirty_segmap()中释放
		if (!dirty_i->dirty_segmap[i])			return -ENOMEM;
		memset(dirty_i->dirty_segmap[i], 0, sizeof(unsigned long) * bitmap_size);
	}

	if (sbi->__is_large_section()) 
	{
		bitmap_size = f2fs_bitmap_size(sbi->MAIN_SECS());
//		dirty_i->dirty_secmap = f2fs_kvzalloc(sbi,bitmap_size, GFP_KERNEL);
		dirty_i->dirty_secmap = new unsigned long[bitmap_size]; // f2fs_kvzalloc<unsigned long>(NULL, bitmap_size);
		if (!dirty_i->dirty_secmap)			return -ENOMEM;
		memset(dirty_i->dirty_secmap, 0, sizeof(unsigned long) * bitmap_size);
	}

	init_dirty_segmap(sbi);
	bitmap_size = f2fs_bitmap_size(sbi->MAIN_SECS());
	return init_victim_secmap(bitmap_size);
}

int f2fs_sb_info::sanity_check_curseg(void)
{
	int i;

	/* In LFS/SSR curseg, .next_blkoff should point to an unused blkaddr;
	 * In LFS curseg, all blkaddr after .next_blkoff should be unused.	 */
	for (i = 0; i < NR_PERSISTENT_LOG; i++)
	{
		curseg_info *curseg = CURSEG_I(i);
		seg_entry *se = get_seg_entry(curseg->segno);
		unsigned int blkofs = curseg->next_blkoff;

		sanity_check_seg_type(this, curseg->seg_type);

		if (f2fs_test_bit(blkofs, (char*)(se->cur_valid_map) ))		goto out;

		if (curseg->alloc_type == SSR)			continue;

		for (blkofs += 1; blkofs < blocks_per_seg; blkofs++) 
		{
			if (!f2fs_test_bit(blkofs, (char*)(se->cur_valid_map)))			continue;
out:
			f2fs_err(sbi,
				 L"Current segment's next free block offset is inconsistent with bitmap, logtype:%u, segno:%u, type:%u, next_blkoff:%u, blkofs:%u",
				 i, curseg->segno, curseg->alloc_type, curseg->next_blkoff, blkofs);
			return -EFSCORRUPTED;
		}
	}
	return 0;
}

#ifdef CONFIG_BLK_DEV_ZONED

static int check_zone_write_pointer(struct f2fs_sb_info *sbi,
				    struct f2fs_dev_info *fdev,
				    struct blk_zone *zone)
{
	unsigned int wp_segno, wp_blkoff, zone_secno, zone_segno, segno;
	block_t zone_block, wp_block, last_valid_block;
	unsigned int log_sectors_per_block = sbi->log_blocksize - SECTOR_SHIFT;
	int i, s, b, ret;
	struct seg_entry *se;

	if (zone->type != BLK_ZONE_TYPE_SEQWRITE_REQ)
		return 0;

	wp_block = fdev->start_blk + (zone->wp >> log_sectors_per_block);
	wp_segno = GET_SEGNO(sbi, wp_block);
	wp_blkoff = wp_block - START_BLOCK(sbi, wp_segno);
	zone_block = fdev->start_blk + (zone->start >> log_sectors_per_block);
	zone_segno = GET_SEGNO(sbi, zone_block);
	zone_secno = GET_SEC_FROM_SEG(sbi, zone_segno);

	if (zone_segno >= sbi->MAIN_SEGS())
		return 0;

	/*
	 * Skip check of zones cursegs point to, since
	 * fix_curseg_write_pointer() checks them.
	 */
	for (i = 0; i < NO_CHECK_TYPE; i++)
		if (zone_secno == GET_SEC_FROM_SEG(sbi,
						   sbi->CURSEG_I( i)->segno))
			return 0;

	/*
	 * Get last valid block of the zone.
	 */
	last_valid_block = zone_block - 1;
	for (s = sbi->segs_per_sec - 1; s >= 0; s--) {
		segno = zone_segno + s;
		se = sbi->get_seg_entry( segno);
		for (b = sbi->blocks_per_seg - 1; b >= 0; b--)
			if (f2fs_test_bit(b, se->cur_valid_map)) {
				last_valid_block = START_BLOCK(sbi, segno) + b;
				break;
			}
		if (last_valid_block >= zone_block)
			break;
	}

	/*
	 * If last valid block is beyond the write pointer, report the
	 * inconsistency. This inconsistency does not cause write error
	 * because the zone will not be selected for write operation until
	 * it get discarded. Just report it.
	 */
	if (last_valid_block >= wp_block) {
		f2fs_notice(sbi, "Valid block beyond write pointer: "
			    "valid block[0x%x,0x%x] wp[0x%x,0x%x]",
			    GET_SEGNO(sbi, last_valid_block),
			    GET_BLKOFF_FROM_SEG0(sbi, last_valid_block),
			    wp_segno, wp_blkoff);
		return 0;
	}

	/*
	 * If there is no valid block in the zone and if write pointer is
	 * not at zone start, reset the write pointer.
	 */
	if (last_valid_block + 1 == zone_block && zone->wp != zone->start) {
		f2fs_notice(sbi,
			    "Zone without valid block has non-zero write "
			    "pointer. Reset the write pointer: wp[0x%x,0x%x]",
			    wp_segno, wp_blkoff);
		ret = __f2fs_issue_discard_zone(sbi, fdev->bdev, zone_block,
					zone->len >> log_sectors_per_block);
		if (ret) {
			f2fs_err(sbi, "Discard zone failed: %s (errno=%d)",
				 fdev->path, ret);
			return ret;
		}
	}

	return 0;
}

static struct f2fs_dev_info *get_target_zoned_dev(struct f2fs_sb_info *sbi,
						  block_t zone_blkaddr)
{
	int i;

	for (i = 0; i < sbi->s_ndevs; i++) {
		if (!bdev_is_zoned(FDEV(i).bdev))
			continue;
		if (sbi->s_ndevs == 1 || (FDEV(i).start_blk <= zone_blkaddr &&
				zone_blkaddr <= FDEV(i).end_blk))
			return &FDEV(i);
	}

	return NULL;
}

static int report_one_zone_cb(struct blk_zone *zone, unsigned int idx,
			      void *data)
{
	memcpy(data, zone, sizeof(struct blk_zone));
	return 0;
}

static int fix_curseg_write_pointer(struct f2fs_sb_info *sbi, int type)
{
	struct curseg_info *cs = sbi->CURSEG_I( type);
	struct f2fs_dev_info *zbd;
	struct blk_zone zone;
	unsigned int cs_section, wp_segno, wp_blkoff, wp_sector_off;
	block_t cs_zone_block, wp_block;
	unsigned int log_sectors_per_block = sbi->log_blocksize - SECTOR_SHIFT;
	sector_t zone_sector;
	int err;

	cs_section = GET_SEC_FROM_SEG(sbi, cs->segno);
	cs_zone_block = START_BLOCK(sbi, GET_SEG_FROM_SEC(sbi, cs_section));

	zbd = get_target_zoned_dev(sbi, cs_zone_block);
	if (!zbd)
		return 0;

	/* report zone for the sector the curseg points to */
	zone_sector = (sector_t)(cs_zone_block - zbd->start_blk)
		<< log_sectors_per_block;
	err = blkdev_report_zones(zbd->bdev, zone_sector, 1,
				  report_one_zone_cb, &zone);
	if (err != 1) {
		f2fs_err(sbi, "Report zone failed: %s errno=(%d)",
			 zbd->path, err);
		return err;
	}

	if (zone.type != BLK_ZONE_TYPE_SEQWRITE_REQ)
		return 0;

	wp_block = zbd->start_blk + (zone.wp >> log_sectors_per_block);
	wp_segno = GET_SEGNO(sbi, wp_block);
	wp_blkoff = wp_block - START_BLOCK(sbi, wp_segno);
	wp_sector_off = zone.wp & GENMASK(log_sectors_per_block - 1, 0);

	if (cs->segno == wp_segno && cs->next_blkoff == wp_blkoff &&
		wp_sector_off == 0)
		return 0;

	f2fs_notice(sbi, "Unaligned curseg[%d] with write pointer: "
		    "curseg[0x%x,0x%x] wp[0x%x,0x%x]",
		    type, cs->segno, cs->next_blkoff, wp_segno, wp_blkoff);

	f2fs_notice(sbi, "Assign new section to curseg[%d]: "
		    "curseg[0x%x,0x%x]", type, cs->segno, cs->next_blkoff);

	f2fs_allocate_new_section(sbi, type, true);

	/* check consistency of the zone curseg pointed to */
	if (check_zone_write_pointer(sbi, zbd, &zone))
		return -EIO;

	/* check newly assigned zone */
	cs_section = GET_SEC_FROM_SEG(sbi, cs->segno);
	cs_zone_block = START_BLOCK(sbi, GET_SEG_FROM_SEC(sbi, cs_section));

	zbd = get_target_zoned_dev(sbi, cs_zone_block);
	if (!zbd)
		return 0;

	zone_sector = (sector_t)(cs_zone_block - zbd->start_blk)
		<< log_sectors_per_block;
	err = blkdev_report_zones(zbd->bdev, zone_sector, 1,
				  report_one_zone_cb, &zone);
	if (err != 1) {
		f2fs_err(sbi, "Report zone failed: %s errno=(%d)",
			 zbd->path, err);
		return err;
	}

	if (zone.type != BLK_ZONE_TYPE_SEQWRITE_REQ)
		return 0;

	if (zone.wp != zone.start) {
		f2fs_notice(sbi,
			    "New zone for curseg[%d] is not yet discarded. "
			    "Reset the zone: curseg[0x%x,0x%x]",
			    type, cs->segno, cs->next_blkoff);
		err = __f2fs_issue_discard_zone(sbi, zbd->bdev,
				zone_sector >> log_sectors_per_block,
				zone.len >> log_sectors_per_block);
		if (err) {
			f2fs_err(sbi, "Discard zone failed: %s (errno=%d)",
				 zbd->path, err);
			return err;
		}
	}

	return 0;
}

int f2fs_fix_curseg_write_pointer(struct f2fs_sb_info *sbi)
{
	int i, ret;

	for (i = 0; i < NR_PERSISTENT_LOG; i++) {
		ret = fix_curseg_write_pointer(sbi, i);
		if (ret)
			return ret;
	}

	return 0;
}

struct check_zone_write_pointer_args {
	struct f2fs_sb_info *sbi;
	struct f2fs_dev_info *fdev;
};

static int check_zone_write_pointer_cb(struct blk_zone *zone, unsigned int idx,
				      void *data)
{
	struct check_zone_write_pointer_args *args;

	args = (struct check_zone_write_pointer_args *)data;

	return check_zone_write_pointer(args->sbi, args->fdev, zone);
}

int f2fs_check_write_pointer(struct f2fs_sb_info *sbi)
{
	int i, ret;
	struct check_zone_write_pointer_args args;

	for (i = 0; i < sbi->s_ndevs; i++) {
		if (!bdev_is_zoned(FDEV(i).bdev))
			continue;

		args.sbi = sbi;
		args.fdev = &FDEV(i);
		ret = blkdev_report_zones(FDEV(i).bdev, 0, BLK_ALL_ZONES,
					  check_zone_write_pointer_cb, &args);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static bool is_conv_zone(struct f2fs_sb_info *sbi, unsigned int zone_idx,
						unsigned int dev_idx)
{
	if (!bdev_is_zoned(FDEV(dev_idx).bdev))
		return true;
	return !test_bit(zone_idx, FDEV(dev_idx).blkz_seq);
}

/* Return the zone index in the given device */
static unsigned int get_zone_idx(struct f2fs_sb_info *sbi, unsigned int secno,
					int dev_idx)
{
	block_t sec_start_blkaddr = START_BLOCK(sbi, GET_SEG_FROM_SEC(sbi, secno));

	return (sec_start_blkaddr - FDEV(dev_idx).start_blk) >>
						sbi->log_blocks_per_blkz;
}

/*
 * Return the usable segments in a section based on the zone's
 * corresponding zone capacity. Zone is equal to a section.
 */
static inline unsigned int f2fs_usable_zone_segs_in_sec(
		struct f2fs_sb_info *sbi, unsigned int segno)
{
	unsigned int dev_idx, zone_idx, unusable_segs_in_sec;

	dev_idx = f2fs_target_device_index(sbi, START_BLOCK(sbi, segno));
	zone_idx = get_zone_idx(sbi, GET_SEC_FROM_SEG(sbi, segno), dev_idx);

	/* Conventional zone's capacity is always equal to zone size */
	if (is_conv_zone(sbi, zone_idx, dev_idx))
		return sbi->segs_per_sec;

	/*
	 * If the zone_capacity_blocks array is NULL, then zone capacity
	 * is equal to the zone size for all zones
	 */
	if (!FDEV(dev_idx).zone_capacity_blocks)
		return sbi->segs_per_sec;

	/* Get the segment count beyond zone capacity block */
	unusable_segs_in_sec = (sbi->blocks_per_blkz -
				FDEV(dev_idx).zone_capacity_blocks[zone_idx]) >>
				sbi->log_blocks_per_seg;
	return sbi->segs_per_sec - unusable_segs_in_sec;
}

/*
 * Return the number of usable blocks in a segment. The number of blocks
 * returned is always equal to the number of blocks in a segment for
 * segments fully contained within a sequential zone capacity or a
 * conventional zone. For segments partially contained in a sequential
 * zone capacity, the number of usable blocks up to the zone capacity
 * is returned. 0 is returned in all other cases.
 */
static inline unsigned int f2fs_usable_zone_blks_in_seg(
			struct f2fs_sb_info *sbi, unsigned int segno)
{
	block_t seg_start, sec_start_blkaddr, sec_cap_blkaddr;
	unsigned int zone_idx, dev_idx, secno;

	secno = GET_SEC_FROM_SEG(sbi, segno);
	seg_start = START_BLOCK(sbi, segno);
	dev_idx = f2fs_target_device_index(sbi, seg_start);
	zone_idx = get_zone_idx(sbi, secno, dev_idx);

	/*
	 * Conventional zone's capacity is always equal to zone size,
	 * so, blocks per segment is unchanged.
	 */
	if (is_conv_zone(sbi, zone_idx, dev_idx))
		return sbi->blocks_per_seg;

	if (!FDEV(dev_idx).zone_capacity_blocks)
		return sbi->blocks_per_seg;

	sec_start_blkaddr = START_BLOCK(sbi, GET_SEG_FROM_SEC(sbi, secno));
	sec_cap_blkaddr = sec_start_blkaddr +
				FDEV(dev_idx).zone_capacity_blocks[zone_idx];

	/*
	 * If segment starts before zone capacity and spans beyond
	 * zone capacity, then usable blocks are from seg start to
	 * zone capacity. If the segment starts after the zone capacity,
	 * then there are no usable blocks.
	 */
	if (seg_start >= sec_cap_blkaddr)
		return 0;
	if (seg_start + sbi->blocks_per_seg > sec_cap_blkaddr)
		return sec_cap_blkaddr - seg_start;

	return sbi->blocks_per_seg;
}
#else
int f2fs_fix_curseg_write_pointer(struct f2fs_sb_info *sbi)
{
	return 0;
}

int f2fs_check_write_pointer(struct f2fs_sb_info *sbi)
{
	return 0;
}

static inline unsigned int f2fs_usable_zone_blks_in_seg(struct f2fs_sb_info *sbi, unsigned int segno)
{
	return 0;
}

static inline unsigned int f2fs_usable_zone_segs_in_sec(struct f2fs_sb_info *sbi, unsigned int segno)
{
	return 0;
}
#endif

unsigned int f2fs_usable_blks_in_seg(struct f2fs_sb_info *sbi,	unsigned int segno)
{
	if (f2fs_sb_has_blkzoned(sbi))	return f2fs_usable_zone_blks_in_seg(sbi, segno);
	return sbi->blocks_per_seg;
}
unsigned int f2fs_usable_segs_in_sec(struct f2fs_sb_info *sbi,	unsigned int segno)
{
	if (f2fs_sb_has_blkzoned(sbi))
		return f2fs_usable_zone_segs_in_sec(sbi, segno);

	return sbi->segs_per_sec;
}

/* Update min, max modified time for cost-benefit GC algorithm */
void f2fs_sb_info::init_min_max_mtime(void)
{
	sit_info *sit_i = sm_info->sit_info;
	unsigned int segno;

	down_write(&sit_i->sentry_lock);

	sit_i->min_mtime = ULLONG_MAX;

	for (segno = 0; segno < MAIN_SEGS(); segno += segs_per_sec)
	{
		unsigned int i;
		unsigned long long mtime = 0;

		for (i = 0; i < segs_per_sec; i++)		mtime += get_seg_entry( segno + i)->mtime;

//		mtime = div_u64(mtime, sbi->segs_per_sec);
		mtime = mtime / segs_per_sec;

		if (sit_i->min_mtime > mtime)			sit_i->min_mtime = mtime;
	}
	sit_i->max_mtime = get_mtime(this, false);
	sit_i->dirty_max_mtime = 0;
	up_write(&sit_i->sentry_lock);
}

int f2fs_sb_info::f2fs_build_segment_manager(void)
{
//	f2fs_sb_info* sbi = this;
	f2fs_super_block *raw_super = F2FS_RAW_SUPER();
	f2fs_checkpoint *ckpt = F2FS_CKPT();
	int err;

//	sm_info = f2fs_kzalloc<f2fs_sm_info>(this, 1/*, GFP_KERNEL*/);
	sm_info = new f2fs_sm_info(raw_super, this);
	if (!sm_info)	return -ENOMEM;
//	memset(sm_info, 0, sizeof(f2fs_sm_info));

	/* init sm info */
//	this->sm_info = sm_info;
	//sm_info->seg0_blkaddr = le32_to_cpu(raw_super->segment0_blkaddr);
	//sm_info->main_blkaddr = le32_to_cpu(raw_super->main_blkaddr);
	//sm_info->segment_count = le32_to_cpu(raw_super->segment_count);
	//sm_info->reserved_segments = le32_to_cpu(ckpt->rsvd_segment_count);
	//sm_info->ovp_segments = le32_to_cpu(ckpt->overprov_segment_count);
	//sm_info->main_segments = le32_to_cpu(raw_super->segment_count_main);
	//sm_info->ssa_blkaddr = le32_to_cpu(raw_super->ssa_blkaddr);
	//sm_info->rec_prefree_segments = sm_info->main_segments * DEF_RECLAIM_PREFREE_SEGMENTS / 100;
	//if (sm_info->rec_prefree_segments > DEF_MAX_RECLAIM_PREFREE_SEGMENTS)
	//	sm_info->rec_prefree_segments = DEF_MAX_RECLAIM_PREFREE_SEGMENTS;

	//if (!f2fs_lfs_mode(this))	sm_info->ipu_policy = 1 << F2FS_IPU_FSYNC;
	//sm_info->min_ipu_util = DEF_MIN_IPU_UTIL;
	//sm_info->min_fsync_blocks = DEF_MIN_FSYNC_BLOCKS;
	//sm_info->min_seq_blocks = blocks_per_seg * segs_per_sec;
	//sm_info->min_hot_blocks = DEF_MIN_HOT_BLOCKS;
	//sm_info->min_ssr_sections = reserved_sections();

	//INIT_LIST_HEAD(&sm_info->sit_entry_set);

	//init_rwsem(&sm_info->curseg_lock);

	if (!f2fs_readonly()) 
	{
		err = f2fs_create_flush_cmd_control();
		if (err)
		{
			LOG_ERROR(L"[err] failed on creating flush cmd control, error=%d", err);
			return err;
		}
	}

	err = create_discard_cmd_control();
	if (err)
	{
		LOG_ERROR(L"[err] failed on creating discard command control, error=%d", err);
		return err;
	}
	err = build_sit_info();
	if (err)
	{
		LOG_ERROR(L"[err] failed on building sit info, error=%d", err);
		return err;
	}
	err = build_free_segmap();
	if (err)
	{
		LOG_ERROR(L"[err] failed on building free segment map, error=%d", err);
		return err;
	}
	err = sm_info->build_curseg(this);
	if (err)
	{
		LOG_ERROR(L"[err] failed on building current segment, error=%d", err);
		return err;
	}
	/* reinit free segmap based on SIT */
	err = build_sit_entries();
	if (err)
	{
		LOG_ERROR(L"[err] failed on building sit entries, error=%d", err);
		return err;
	}

	init_free_segmap();
	err = sm_info->build_dirty_segmap(this);
	if (err)
	{
		LOG_ERROR(L"[err] failed on building dirty segment map, error=%d", err);
		return err;
	}
	err = sanity_check_curseg();
	if (err)
	{
		LOG_ERROR(L"[err] failed on sanity check current segment, error=%d", err);
		return err;
	}

	init_min_max_mtime();
	return 0;
}

f2fs_sm_info::f2fs_sm_info(f2fs_super_block* raw_super, f2fs_sb_info* sbi)
{
	f2fs_checkpoint* ckpt = sbi->F2FS_CKPT();

	seg0_blkaddr = le32_to_cpu(raw_super->segment0_blkaddr);
	main_blkaddr = le32_to_cpu(raw_super->main_blkaddr);
	segment_count = le32_to_cpu(raw_super->segment_count);
	reserved_segments = le32_to_cpu(ckpt->rsvd_segment_count);
	ovp_segments = le32_to_cpu(ckpt->overprov_segment_count);
	main_segments = le32_to_cpu(raw_super->segment_count_main);
	ssa_blkaddr = le32_to_cpu(raw_super->ssa_blkaddr);
	rec_prefree_segments = main_segments * DEF_RECLAIM_PREFREE_SEGMENTS / 100;
	if (rec_prefree_segments > DEF_MAX_RECLAIM_PREFREE_SEGMENTS)
		rec_prefree_segments = DEF_MAX_RECLAIM_PREFREE_SEGMENTS;

	if (!f2fs_lfs_mode(sbi))	ipu_policy = 1 << F2FS_IPU_FSYNC;
	min_ipu_util = DEF_MIN_IPU_UTIL;
	min_fsync_blocks = DEF_MIN_FSYNC_BLOCKS;
	min_seq_blocks = sbi->blocks_per_seg * sbi->segs_per_sec;
	min_hot_blocks = DEF_MIN_HOT_BLOCKS;
	min_ssr_sections = reserved_sections(sbi->segs_per_sec);

	INIT_LIST_HEAD(&sit_entry_set);

	init_rwsem(&curseg_lock);
}


//static void discard_dirty_segmap(struct f2fs_sb_info *sbi, enum dirty_type dirty_type)
void f2fs_sm_info::discard_dirty_segmap(enum dirty_type dirty_type)
{
//	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	mutex_lock(&dirty_info->seglist_lock);
//	kvfree(dirty_info->dirty_segmap[dirty_type]);
	f2fs_kvfree(dirty_info->dirty_segmap[dirty_type]);	//<PAIR>f2fs_sm_info::build_dirty_segmap中申请
	dirty_info->nr_dirty[dirty_type] = 0;
	mutex_unlock(&dirty_info->seglist_lock);
}

//static void destroy_victim_secmap(struct f2fs_sb_info* sbi)
void f2fs_sm_info::destroy_victim_secmap()
{
//	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
//	kvfree(dirty_i->victim_secmap);
	delete [] dirty_info->victim_secmap;
}

//static void destroy_dirty_segmap(struct f2fs_sb_info *sbi)
void f2fs_sm_info::destroy_dirty_segmap(void)
{
//	struct dirty_seglist_info *dirty_info = dirty_info(sbi);
//	int i;
	if (!dirty_info)	return;
	/* discard pre-free/dirty segments list */
	for (int ii = 0; ii < NR_DIRTY_TYPE; ii++)	discard_dirty_segmap((dirty_type)(ii));

//	if (sbi->__is_large_section())
	{
		mutex_lock(&dirty_info->seglist_lock);
//		kvfree(dirty_info->dirty_secmap);
		delete [] dirty_info->dirty_secmap;
		mutex_unlock(&dirty_info->seglist_lock);
	}
	destroy_victim_secmap();
	//sbi->SM_I()->dirty_info = NULL;
	//kfree(dirty_info);
	delete dirty_info;
	dirty_info = nullptr;
}

//static void destroy_curseg(struct f2fs_sb_info *sbi)
void f2fs_sm_info::destroy_curseg(void)
{
//	struct curseg_info *array = sbi->SM_I()->curseg_array;
//	int i;

	if (!curseg_array)		return;
//	sbi->SM_I()->curseg_array = NULL;
	for (int i = 0; i < NR_CURSEG_TYPE; i++) 
	{
//		kfree(curseg_array[i].sum_blk);
//		kfree(curseg_array[i].journal);
//		delete curseg_array[i].sum_blk;
//		delete curseg_array[i].journal;
	}
//	kfree(curseg_array);
	delete[] curseg_array;
	curseg_array = nullptr;
}

//static void destroy_free_segmap(struct f2fs_sb_info *sbi)
void f2fs_sm_info::destroy_free_segmap(void)
{
//	struct free_segmap_info *free_i = sbi->SM_I()->free_info;

	if (!free_info)	return;
//	sbi->SM_I()->free_info = NULL;
//	kvfree(free_info->free_segmap);
//	kvfree(free_info->free_secmap);
	delete []free_info->free_segmap;
	delete[] free_info->free_secmap;
//	kfree(free_i);

	delete free_info;
	free_info = nullptr;
}

//static void destroy_sit_info(struct f2fs_sb_info *sbi)
void f2fs_sm_info::destroy_sit_info(void)
{
//	struct sit_info *sit_i = sbi->SIT_I();
	if (!sit_info)	return;

	if (sit_info->sentries)	delete [] sit_info->bitmap;//		kvfree(sit_i->bitmap);
	//kfree(sit_i->tmp_map);
	//kvfree(sit_i->sentries);
	//kvfree(sit_i->dirty_sentries_bitmap);
	delete [] sit_info->tmp_map;
	delete [] sit_info->sentries;
	f2fs_kvfree(sit_info->sec_entries);			//<PAIR> f2fs_sb_info::build_sit_info()中alloc
	delete [] sit_info->dirty_sentries_bitmap;

	f2fs_kvfree(sit_info->sit_bitmap);
#ifdef CONFIG_F2FS_CHECK_FS
	kvfree(sit_i->sit_bitmap_mir);
	kvfree(sit_i->invalid_segmap);
#endif
//	kfree(sit_i);
	delete sit_info;
	sit_info = nullptr;
}

//void f2fs_destroy_segment_manager(struct f2fs_sb_info *sbi)
void f2fs_sb_info::f2fs_destroy_segment_manager()
{
//	struct f2fs_sm_info *sm_info = sbi->SM_I();
	if (!sm_info)	return;
	//if (fcc_info)
	//{
	//	fcc_info->f2fs_destroy_flush_cmd_control(true);
	//	delete fcc_info;
	//	fcc_info = nullptr;
	//}
	//if (dcc_info)
	//{
	//	dcc_info->destroy_discard_cmd_control();
	//	delete dcc_info;
	//	fcc_info = nullptr;
	//}
	//destroy_dirty_segmap();
	//destroy_curseg();
	//destroy_free_segmap();
	//destroy_sit_info();
	delete sm_info;
	sm_info = nullptr;
	//kfree(sm_info);
}

f2fs_sm_info::~f2fs_sm_info(void)
{
	if (fcc_info)	f2fs_destroy_flush_cmd_control(true);
//	{
//		fcc_info->f2fs_destroy_flush_cmd_control(true);
		//delete fcc_info;
		//fcc_info = nullptr;
//	}
	if (dcc_info)
	{
		dcc_info->destroy_discard_cmd_control();
		delete dcc_info;
		fcc_info = nullptr;
	}
	destroy_dirty_segmap();
	destroy_curseg();
	destroy_free_segmap();
	destroy_sit_info();
}

#if 0 //<TODO>

int __init f2fs_create_segment_manager_caches(void)
{
	discard_entry_slab = f2fs_kmem_cache_create("f2fs_discard_entry",
			sizeof(struct discard_entry));
	if (!discard_entry_slab)
		goto fail;

	discard_cmd_slab = f2fs_kmem_cache_create("f2fs_discard_cmd",
			sizeof(struct discard_cmd));
	if (!discard_cmd_slab)
		goto destroy_discard_entry;

	sit_entry_set_slab = f2fs_kmem_cache_create("f2fs_sit_entry_set",
			sizeof(struct sit_entry_set));
	if (!sit_entry_set_slab)
		goto destroy_discard_cmd;

	inmem_entry_slab = f2fs_kmem_cache_create("f2fs_inmem_page_entry",
			sizeof(struct inmem_pages));
	if (!inmem_entry_slab)
		goto destroy_sit_entry_set;
	return 0;

destroy_sit_entry_set:
	kmem_cache_destroy(sit_entry_set_slab);
destroy_discard_cmd:
	kmem_cache_destroy(discard_cmd_slab);
destroy_discard_entry:
	kmem_cache_destroy(discard_entry_slab);
fail:
	return -ENOMEM;
}

void f2fs_destroy_segment_manager_caches(void)
{
	kmem_cache_destroy(sit_entry_set_slab);
	kmem_cache_destroy(discard_cmd_slab);
	kmem_cache_destroy(discard_entry_slab);
	kmem_cache_destroy(inmem_entry_slab);
}

#endif //TODO

/* Summary block is always treated as an invalid block */
int f2fs_sb_info::check_block_count(int segno, struct f2fs_sit_entry* raw_sit)
{
	bool is_valid = test_bit_le(0, raw_sit->valid_map) ? true : false;
	UINT64 valid_blocks = 0;
	UINT64 cur_pos = 0, next_pos;
	unsigned int usable_blks_per_seg = f2fs_usable_blks_in_seg(this, segno);

	/* check bitmap with valid block count */
	do
	{
		if (is_valid)
		{
			next_pos = find_next_zero_bit_le((UINT8*)(&raw_sit->valid_map), usable_blks_per_seg, cur_pos);
			valid_blocks += next_pos - cur_pos;
		}
		else		next_pos = find_next_bit_le((UINT8*)(&raw_sit->valid_map), usable_blks_per_seg, cur_pos);
		cur_pos = next_pos;
		is_valid = !is_valid;
	} while (cur_pos < usable_blks_per_seg);

	if (unlikely(GET_SIT_VBLOCKS(raw_sit) != valid_blocks))
	{
		f2fs_err(this, L"Mismatch valid blocks %d vs. %lld", GET_SIT_VBLOCKS(raw_sit), valid_blocks);
		set_sbi_flag(SBI_NEED_FSCK);
		return -EFSCORRUPTED;
	}

	if (usable_blks_per_seg < blocks_per_seg)
	{
		f2fs_bug_on(this, find_next_bit_le((UINT8*)(&raw_sit->valid_map), blocks_per_seg, usable_blks_per_seg) !=
			blocks_per_seg);
	}

	/* check segment usage, and check boundary of a given segment number */
	if (unlikely(GET_SIT_VBLOCKS(raw_sit) > usable_blks_per_seg || segno > TOTAL_SEGS(this) - 1))
	{
		f2fs_err(this, L"Wrong valid blocks %d or segno %u", GET_SIT_VBLOCKS(raw_sit), segno);
		set_sbi_flag(SBI_NEED_FSCK);
		return -EFSCORRUPTED;
	}
	return 0;
}

				
block_t			f2fs_sb_info::written_block_count(void) { return (sm_info->sit_info->written_valid_blocks); }
unsigned int	f2fs_sb_info::free_segments(void)const { return sm_info->free_info->free_segments; }
int				f2fs_sb_info::overprovision_segments(void) { return sm_info->ovp_segments; }
unsigned int	f2fs_sb_info::prefree_segments(void) { return sm_info->dirty_info->nr_dirty[PRE]; }


//static inline bool has_not_enough_free_secs(f2fs_sb_info* sbi, int freed, int needed)
bool f2fs_sb_info::has_not_enough_free_secs(int freed, int needed)
{
	int node_secs = get_blocktype_secs(F2FS_DIRTY_NODES);
	int dent_secs = get_blocktype_secs(F2FS_DIRTY_DENTS);
	int imeta_secs = get_blocktype_secs(F2FS_DIRTY_IMETA);

	if (unlikely(is_sbi_flag_set(SBI_POR_DOING)))		return false;
	UINT free_s = free_sections();
	int free_ed = free_sections() + freed;
	int reserved = reserved_sections();
	int requested = (node_secs + 2 * dent_secs + imeta_secs + reserved + needed);
	bool not_enough = free_ed <= requested;

	LOG_DEBUG_(1,L"node_secs=%d, dent_secs=%d, imeta_secs=%d, free_section=%d, reserved=%d, freed=%d, needed=%d", node_secs, dent_secs, imeta_secs, free_s, reserved, freed, needed);
	LOG_DEBUG_(1,L"total_free=%d, requested=%d, %s enough space", free_ed, requested, not_enough?L"NOT":L"");

	if (free_ed == reserved + needed && has_curseg_enough_space())	return false;
	return (not_enough);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== sit_info ====
sit_info::sit_info(void)
{
	min_mtime=0;		/* min. modification time */
	max_mtime=0;		/* max. modification time */
	dirty_min_mtime=0;	/* rerange candidates in GC_AT */
	dirty_max_mtime=0;	/* rerange candidates in GC_AT */
	memset(last_victim, 0, sizeof(last_victim));
}

sit_info::~sit_info(void)
{
}


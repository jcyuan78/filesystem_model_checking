///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include <linux-fs-wrapper.h>
#include "../include/f2fs_fs.h"
#include "../include/f2fs.h"
#include "../include/discard-control.h"
#include "../include/f2fs_fs.h"
#include "../include/f2fs-filesystem.h"
#include "../include/io-complete-ctrl.h"
#include "f2fs/node.h"
#include "f2fs/segment.h"

LOCAL_LOGGER_ENABLE(L"f2fs.discard", LOGGER_LEVEL_DEBUGINFO);


/// <summary>
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== static functions
static void __check_sit_bitmap(struct f2fs_sb_info* sbi, block_t start, block_t end)
{
#ifdef CONFIG_F2FS_CHECK_FS
	struct seg_entry* sentry;
	unsigned int segno;
	block_t blk = start;
	unsigned long offset, size, max_blocks = sbi->blocks_per_seg;
	unsigned long* map;

	while (blk < end)
	{
		segno = GET_SEGNO(sbi, blk);
		sentry = sbi->get_seg_entry( segno);
		offset = GET_BLKOFF_FROM_SEG0(sbi, blk);

		if (end < START_BLOCK(sbi, segno + 1))
			size = GET_BLKOFF_FROM_SEG0(sbi, end);
		else
			size = max_blocks;
		map = (unsigned long*)(sentry->cur_valid_map);
		offset = __find_rev_next_bit(map, size, offset);
		f2fs_bug_on(sbi, offset != size);
		blk = START_BLOCK(sbi, segno + 1);
	}
#endif
}


static void f2fs_submit_discard_endio(struct bio* bio)
{
	discard_cmd* dc = (discard_cmd*)bio->bi_private;
//	unsigned long flags;	<YUAN> unuse

	spin_lock_irqsave(&dc->lock, flags);
	if (!dc->error)		dc->error = blk_status_to_errno(bio->bi_status);
	dc->bio_ref--;
	if (!dc->bio_ref && dc->state == D_SUBMIT)
	{
		dc->state = D_DONE;
		complete_all(&dc->wait);
	}
	spin_unlock_irqrestore(&dc->lock, flags);
	LOG_TRACK(L"bio", L"bio=%p, call bio_put", bio);
	bio_put(bio);
}

/// <summary>
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// </summary>
/// <param name="discard_cmd_control"></param>



discard_cmd_control::discard_cmd_control(f2fs_sb_info* sbi)
{
	m_sbi = sbi;

	discard_granularity = DEFAULT_DISCARD_GRANULARITY;
	INIT_LIST_HEAD(&entry_list);
	for (int i = 0; i < MAX_PLIST_NUM; i++)		INIT_LIST_HEAD(&pend_list[i]);
	INIT_LIST_HEAD(&wait_list);
	INIT_LIST_HEAD(&fstrim_list);
	mutex_init(&cmd_lock);
	atomic_set(&issued_discard, 0);
	atomic_set(&queued_discard, 0);
	atomic_set(&discard_cmd_cnt, 0);
	nr_discards = 0;
	max_discards = m_sbi->MAIN_SEGS() << m_sbi->log_blocks_per_seg;
	undiscard_blks = 0;
	next_pos = 0;
	//	root = RB_ROOT_CACHED;
	memset(&root, 0, sizeof(root));
	//root.rb_leftmost->rb_left = NULL;
	//root.rb_leftmost->rb_right = NULL;
	//root.rb_leftmost->__rb_parent_color = NULL;
	//root.rb_root.rb_node->rb_left = NULL;

#ifdef _DEBUG
	memset(n_pend_list, 0, sizeof(n_pend_list));
#endif
	rbtree_check = false;

}

DWORD discard_cmd_control::issue_discard_thread(void)
{
	LOG_STACK_TRACE();
	//discard_cmd_control *dcc = sm_info->dcc_info;
//#if 0
//	wait_queue_head_t *q = &dcc->discard_wait_queue;
//#endif
	struct discard_policy dpolicy;
	unsigned int wait_ms = DEF_MIN_DISCARD_ISSUE_TIME;
	int issued;

#if 0
	set_freezable();
#endif
	InterlockedExchange(&m_started, 1);
	do
	{

		if (m_sbi->gc_mode == GC_URGENT_HIGH || !f2fs_available_free_memory(m_sbi, DISCARD_CACHE))
			__init_discard_policy(&dpolicy, DPOLICY_FORCE, 1);
		else
			__init_discard_policy(&dpolicy, DPOLICY_BG, discard_granularity);

		if (!atomic_read(&discard_cmd_cnt))       wait_ms = dpolicy.max_interval;

		LOG_DEBUG_(1,L"waiting for que");
		WaitForSingleObject(m_que_event, wait_ms);
		//wait_event_interruptible_timeout(*q, kthread_should_stop() || freezing(current) || discard_wake, 	msecs_to_jiffies(wait_ms));
		if (m_running == 0)
		{
			LOG_DEBUG_(1,L"stop running by host");
			break;
		}
		LOG_DEBUG_(1,L"processing discard cmd");
		if (discard_wake)		discard_wake = 0;
		/* clean up pending candidates before going to sleep */
		if (atomic_read(&queued_discard))	__wait_all_discard_cmd(NULL);

#if 0
		if (try_to_freeze())			continue;
#endif
		if (m_sbi->f2fs_readonly())			continue;
		//		if (kthread_should_stop())		return 0;
		if (m_running == 0)			return 0;
		if (m_sbi->is_sbi_flag_set(SBI_NEED_FSCK))
		{
			wait_ms = dpolicy.max_interval;
			continue;
		}
		if (!atomic_read(&discard_cmd_cnt))			continue;

		sb_start_intwrite(m_sbi);

		issued = __issue_discard_cmd(&dpolicy);
		if (issued > 0)
		{
			__wait_all_discard_cmd(&dpolicy);
			wait_ms = dpolicy.min_interval;
		}
		else if (issued == -1)
		{
			wait_ms = f2fs_time_to_wait(m_sbi, DISCARD_TIME);
			if (!wait_ms)				wait_ms = dpolicy.mid_interval;
		}
		else
		{
			wait_ms = dpolicy.max_interval;
		}

		sb_end_intwrite(m_sbi);

	}
	//	while (!kthread_should_stop());
	while (m_running);
	return 0;
}

discard_cmd* discard_cmd_control::__create_discard_cmd(block_device* bdev, block_t lstart, block_t start, block_t len)
{
	//discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
//	struct list_head *pend_list;
	struct discard_cmd* dc;

	f2fs_bug_on(m_sbi, !len);
	int index = plist_idx(len);
	list_head* local_pend_list = &pend_list[index];

	//	dc = f2fs_kmem_cache_alloc(discard_cmd_slab, GFP_NOFS);
	dc = new discard_cmd;
	INIT_LIST_HEAD(&dc->list);
	dc->bdev = bdev;
	dc->lstart = lstart;
	dc->start = start;
	dc->len = len;
	dc->ref = 0;
	dc->state = D_PREP;
	dc->queued = 0;
	dc->error = 0;
	init_completion(&dc->wait);
	list_add_tail(&dc->list, local_pend_list);
#ifdef _DEBUG
	atomic_inc(&(n_pend_list[index]));
	LOG_DEBUG_(1,L"add cmd to list %d, size=%d", index, n_pend_list[index]);
#endif

	spin_lock_init(&dc->lock);
	dc->bio_ref = 0;
	atomic_inc(&discard_cmd_cnt);
	undiscard_blks += len;
	SetEvent(m_que_event);
	return dc;
}

discard_cmd* discard_cmd_control::__attach_discard_cmd(block_device* bdev, block_t lstart,
	block_t start, block_t len, rb_node* parent, rb_node** p, bool leftmost)
{
	//discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	discard_cmd* dc;

	dc = __create_discard_cmd(bdev, lstart, start, len);

	rb_link_node(&dc->rb_node, parent, p);
	rb_insert_color_cached(&dc->rb_node, &root, leftmost);

	return dc;
}

void discard_cmd_control::__detach_discard_cmd(discard_cmd* dc)
{
	if (dc->state == D_DONE) atomic_sub(dc->queued, &queued_discard);

	list_del(&dc->list);
	rb_erase_cached(&dc->rb_node, &root);
	undiscard_blks -= dc->len;
	//	kmem_cache_free(discard_cmd_slab, dc);
	delete dc;
	atomic_dec(&discard_cmd_cnt);
}

void discard_cmd_control::__remove_discard_cmd(discard_cmd* dc)
{
	//discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
//	unsigned long flags; //<UNUSE>

	//trace_f2fs_remove_discard(dc->bdev, dc->start, dc->len);

	spin_lock_irqsave(&dc->lock, flags);
	if (dc->bio_ref)
	{
		spin_unlock_irqrestore(&dc->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&dc->lock, flags);

	f2fs_bug_on(m_sbi, dc->ref);

	if (dc->error == -EOPNOTSUPP)	dc->error = 0;

	if (dc->error)
		//printk_ratelimited(
		LOG_ERROR(L"[err] F2FS-fs (%s): Issue discard(%u, %u, %u) failed, ret: %d",
			/*KERN_INFO,*/ m_sbi->s_id, dc->lstart, dc->start, dc->len, dc->error);
	__detach_discard_cmd(dc);
}


void discard_cmd_control::__init_discard_policy(discard_policy* dpolicy, int discard_type, unsigned int granularity)
{
	//	discard_cmd_control *dcc = sbi->SM_I()->dcc_info;

		/* common policy */
	dpolicy->type = discard_type;
	dpolicy->sync = true;
	dpolicy->ordered = false;
	dpolicy->granularity = granularity;

	dpolicy->max_requests = DEF_MAX_DISCARD_REQUEST;
	dpolicy->io_aware_gran = MAX_PLIST_NUM;
	dpolicy->timeout = false;

	if (discard_type == DPOLICY_BG)
	{
		dpolicy->min_interval = DEF_MIN_DISCARD_ISSUE_TIME;
		dpolicy->mid_interval = DEF_MID_DISCARD_ISSUE_TIME;
		dpolicy->max_interval = DEF_MAX_DISCARD_ISSUE_TIME;
		dpolicy->io_aware = true;
		dpolicy->sync = false;
		dpolicy->ordered = true;
		if (m_sbi->utilization() > DEF_DISCARD_URGENT_UTIL)
		{
			dpolicy->granularity = 1;
			if (atomic_read(&discard_cmd_cnt))	dpolicy->max_interval = DEF_MIN_DISCARD_ISSUE_TIME;
		}
	}
	else if (discard_type == DPOLICY_FORCE)
	{
		dpolicy->min_interval = DEF_MIN_DISCARD_ISSUE_TIME;
		dpolicy->mid_interval = DEF_MID_DISCARD_ISSUE_TIME;
		dpolicy->max_interval = DEF_MAX_DISCARD_ISSUE_TIME;
		dpolicy->io_aware = false;
	}
	else if (discard_type == DPOLICY_FSTRIM)
	{
		dpolicy->io_aware = false;
	}
	else if (discard_type == DPOLICY_UMOUNT)
	{
		dpolicy->io_aware = false;
		/* we need to issue all to keep CP_TRIMMED_FLAG */
		dpolicy->granularity = 1;
		dpolicy->timeout = true;
	}
}

void discard_cmd_control::__insert_discard_tree(block_device* bdev, block_t lstart,
	block_t start, block_t len,
	struct rb_node** insert_p,
	struct rb_node* insert_parent)
{
	//discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	struct rb_node** p;
	struct rb_node* parent = NULL;
	bool leftmost = true;

	if (insert_p && insert_parent)
	{
		parent = insert_parent;
		p = insert_p;
		goto do_insert;
	}

	p = f2fs_lookup_rb_tree_for_insert(m_sbi, &root, &parent, lstart, &leftmost);
do_insert:
	__attach_discard_cmd(bdev, lstart, start, len, parent, p, leftmost);
}

void discard_cmd_control::__relocate_discard_cmd(discard_cmd* dc)
{
	list_move_tail(&dc->list, &pend_list[plist_idx(dc->len)]);
}

void discard_cmd_control::__update_discard_tree_range(block_device* bdev, block_t lstart, block_t start, block_t len)
{
	//discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	struct discard_cmd* prev_dc = NULL, * next_dc = NULL;
	struct discard_cmd* dc;
	struct discard_info di = { 0 };
	struct rb_node** insert_p = NULL, * insert_parent = NULL;
	//struct request_queue *q = bdev_get_queue(bdev);
	//unsigned int max_discard_blocks = SECTOR_TO_BLOCK(q->limits.max_discard_sectors);
	unsigned int max_discard_blocks = SECTOR_TO_BLOCK(MAX_DISCARD_SECTOR);
	block_t end = lstart + len;

	dc = (discard_cmd*)f2fs_lookup_rb_tree_ret(&root, NULL, lstart,
		(rb_entry**)&prev_dc,
		(rb_entry**)&next_dc,
		&insert_p, &insert_parent, true, NULL);
	if (dc)	prev_dc = dc;

	if (!prev_dc)
	{
		di.lstart = lstart;
		di.len = next_dc ? next_dc->lstart - lstart : len;
		di.len = min(di.len, len);
		di.start = start;
	}

	while (1)
	{
		struct rb_node* node;
		bool merged = false;
		struct discard_cmd* tdc = NULL;

		if (prev_dc)
		{
			di.lstart = prev_dc->lstart + prev_dc->len;
			if (di.lstart < lstart)
				di.lstart = lstart;
			if (di.lstart >= end)
				break;

			if (!next_dc || next_dc->lstart > end)
				di.len = end - di.lstart;
			else
				di.len = next_dc->lstart - di.lstart;
			di.start = start + di.lstart - lstart;
		}

		if (!di.len)
			goto next;

		if (prev_dc && prev_dc->state == D_PREP &&
			prev_dc->bdev == bdev &&
			__is_discard_back_mergeable(&di, &prev_dc->di, max_discard_blocks))
		{
			prev_dc->di.len += di.len;
			undiscard_blks += di.len;
			__relocate_discard_cmd(prev_dc);
			di = prev_dc->di;
			tdc = prev_dc;
			merged = true;
		}

		if (next_dc && next_dc->state == D_PREP &&
			next_dc->bdev == bdev && __is_discard_front_mergeable(&di, &next_dc->di, max_discard_blocks))
		{
			next_dc->di.lstart = di.lstart;
			next_dc->di.len += di.len;
			next_dc->di.start = di.start;
			undiscard_blks += di.len;
			__relocate_discard_cmd(next_dc);
			if (tdc)	__remove_discard_cmd(tdc);
			merged = true;
		}

		if (!merged)
		{
			__insert_discard_tree(bdev, di.lstart, di.start, di.len, NULL, NULL);
		}
	next:
		prev_dc = next_dc;
		if (!prev_dc)
			break;

		node = rb_next(&prev_dc->rb_node);
		next_dc = rb_entry_safe(node, discard_cmd, rb_node);
		//		(node ? rb_entry(node, discard_cmd, rb_node) : NULL)
	}
}


unsigned int discard_cmd_control::__issue_discard_cmd_orderly(discard_policy* dpolicy)
{
	//	discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	struct discard_cmd* prev_dc = NULL, * next_dc = NULL;
	struct rb_node** insert_p = NULL, * insert_parent = NULL;
	struct discard_cmd* dc;
	struct blk_plug plug;
	unsigned int pos = next_pos;
	unsigned int issued = 0;
	bool io_interrupted = false;

	mutex_lock(&cmd_lock);
	dc = (discard_cmd*)f2fs_lookup_rb_tree_ret(&root, NULL, pos,
		(rb_entry**)&prev_dc, (rb_entry**)&next_dc, &insert_p, &insert_parent, true, NULL);
	if (!dc) dc = next_dc;

	blk_start_plug(&plug);

	while (dc)
	{
		struct rb_node* node;
		int err = 0;

		if (dc->state != D_PREP)			goto next;

		if (dpolicy->io_aware && !is_idle(m_sbi, DISCARD_TIME))
		{
			io_interrupted = true;
			break;
		}

		next_pos = dc->lstart + dc->len;
		err = __submit_discard_cmd(dpolicy, dc, &issued);

		if (issued >= dpolicy->max_requests)
			break;
	next:
		node = rb_next(&dc->rb_node);
		if (err)		__remove_discard_cmd(dc);
		dc = rb_entry_safe(node, struct discard_cmd, rb_node);
	}

	blk_finish_plug(&plug);

	if (!dc)	next_pos = 0;

	mutex_unlock(&cmd_lock);

	if (!issued && io_interrupted)		issued = -1;

	return issued;
}

int discard_cmd_control::__issue_discard_cmd(discard_policy* dpolicy)
{
	//discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
//	struct list_head *pend_list;
	struct discard_cmd* dc, * tmp;
	struct blk_plug plug;
	int i;
	bool io_interrupted = false;

	if (dpolicy->timeout)	m_sbi->f2fs_update_time(UMOUNT_DISCARD_TIMEOUT);

retry:
	unsigned int issued = 0;
	for (i = MAX_PLIST_NUM - 1; i >= 0; i--)
	{
		if (dpolicy->timeout && f2fs_time_over(m_sbi, UMOUNT_DISCARD_TIMEOUT))			break;
		if (i + 1 < dpolicy->granularity)			break;

		if (i < DEFAULT_DISCARD_GRANULARITY && dpolicy->ordered)	return __issue_discard_cmd_orderly(dpolicy);

		list_head* local_pend_list = &pend_list[i];

		mutex_lock(&cmd_lock);
		if (list_empty(local_pend_list))		goto next;
		if (unlikely(rbtree_check))		f2fs_bug_on(m_sbi, !f2fs_check_rb_tree_consistence(m_sbi, &root, false));
		blk_start_plug(&plug);
#ifdef _DEBUG
		LOG_DEBUG_(1,L"check discoard cmd, list=%d, size=%d", i, n_pend_list[i]);
#endif
		list_for_each_entry_safe(discard_cmd, dc, tmp, local_pend_list, list)
		{
			f2fs_bug_on(m_sbi, dc->state != D_PREP);

			if (dpolicy->timeout && f2fs_time_over(m_sbi, UMOUNT_DISCARD_TIMEOUT))		break;
			if (dpolicy->io_aware && i < dpolicy->io_aware_gran && !is_idle(m_sbi, DISCARD_TIME))
			{
				io_interrupted = true;
				break;
			}

			__submit_discard_cmd(dpolicy, dc, &issued);

			if (issued >= dpolicy->max_requests)	break;
		}
		blk_finish_plug(&plug);
	next:
		mutex_unlock(&cmd_lock);

		if (issued >= dpolicy->max_requests || io_interrupted)	break;
	}

	if (dpolicy->type == DPOLICY_UMOUNT && issued)
	{
		__wait_all_discard_cmd(dpolicy);
		goto retry;
	}

	if (!issued && io_interrupted)		issued = -1;

	return issued;
}


unsigned int discard_cmd_control::__wait_one_discard_bio(discard_cmd* dc)
{
	//discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	unsigned int len = 0;

	wait_for_completion_io(&dc->wait);
	mutex_lock(&cmd_lock);
	f2fs_bug_on(m_sbi, dc->state != D_DONE);
	dc->ref--;
	if (!dc->ref)
	{
		if (!dc->error)
			len = dc->len;
		__remove_discard_cmd(dc);
	}
	mutex_unlock(&cmd_lock);

	return len;
}


unsigned int discard_cmd_control::__wait_discard_cmd_range(struct discard_policy* dpolicy, block_t start, block_t end)
{
	//discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	struct list_head* local_wait_list = (dpolicy->type == DPOLICY_FSTRIM) ? &(fstrim_list) : &(wait_list);
	struct discard_cmd* dc, * tmp;
	bool need_wait;
	unsigned int trimmed = 0;

next:
	need_wait = false;

	mutex_lock(&cmd_lock);
	list_for_each_entry_safe(discard_cmd, dc, tmp, local_wait_list, list)
	{
		if (dc->lstart + dc->len <= start || end <= dc->lstart)
			continue;
		if (dc->len < dpolicy->granularity)
			continue;
		if (dc->state == D_DONE && !dc->ref)
		{
			wait_for_completion_io(&dc->wait);
			if (!dc->error)
				trimmed += dc->len;
			__remove_discard_cmd(dc);
		}
		else
		{
			dc->ref++;
			need_wait = true;
			break;
		}
	}
	mutex_unlock(&cmd_lock);

	if (need_wait)
	{
		trimmed += __wait_one_discard_bio(dc);
		goto next;
	}

	return trimmed;
}

unsigned int discard_cmd_control::__wait_all_discard_cmd(discard_policy* dpolicy)
{
	discard_policy dp;
	unsigned int discard_blks;

	if (dpolicy)	return __wait_discard_cmd_range(dpolicy, 0, UINT_MAX);

	/* wait all */
	__init_discard_policy(&dp, DPOLICY_FSTRIM, 1);
	discard_blks = __wait_discard_cmd_range(&dp, 0, UINT_MAX);
	__init_discard_policy(&dp, DPOLICY_UMOUNT, 1);
	discard_blks += __wait_discard_cmd_range(&dp, 0, UINT_MAX);

	return discard_blks;
}

/* this function is copied from blkdev_issue_discard from block/blk-lib.c */
int discard_cmd_control::__submit_discard_cmd(discard_policy* dpolicy, discard_cmd* dc, unsigned int* issued)
{
	block_device* bdev = dc->bdev;
	//struct request_queue *q = bdev_get_queue(bdev);
//	unsigned int max_discard_blocks = SECTOR_TO_BLOCK(q->limits.max_discard_sectors);
	unsigned int max_discard_blocks = SECTOR_TO_BLOCK(MAX_DISCARD_SECTOR);
	//discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
	struct list_head* local_wait_list = (dpolicy->type == DPOLICY_FSTRIM) ? &(fstrim_list) : &(wait_list);
	int flag = dpolicy->sync ? REQ_SYNC : 0;
	block_t lstart, start, len, total_len;
	int err = 0;

	if (dc->state != D_PREP)		return 0;

	if (m_sbi->is_sbi_flag_set(SBI_NEED_FSCK))		return 0;

//	trace_f2fs_issue_discard(bdev, dc->start, dc->len);
	lstart = dc->lstart;
	start = dc->start;
	len = dc->len;
	total_len = len;

	dc->len = 0;
	//CF2fsFileSystem* fs = m_sbi->m_fs;

	while (total_len && *issued < dpolicy->max_requests && !err)
	{
		struct bio* bio = NULL;
//		unsigned long flags;	<UNUSE>
		bool last = true;

		if (len > max_discard_blocks)
		{
			len = max_discard_blocks;
			last = false;
		}

		(*issued)++;
		if (*issued == dpolicy->max_requests)			last = true;

		dc->len += len;

		if (time_to_inject(m_sbi, FAULT_DISCARD))
		{
			f2fs_show_injection_info(m_sbi, FAULT_DISCARD);
			err = -EIO;
			goto submit;
		}
		err = m_sbi->__blkdev_issue_discard(bdev, SECTOR_FROM_BLOCK(start), SECTOR_FROM_BLOCK(len), GFP_NOFS, 0, &bio);
	submit:
		if (err)
		{
			spin_lock_irqsave(&dc->lock, flags);
			if (dc->state == D_PARTIAL)				dc->state = D_SUBMIT;
			spin_unlock_irqrestore(&dc->lock, flags);
			break;
		}

		f2fs_bug_on(m_sbi, !bio);

		/* should keep before submission to avoid D_DONE right away	 */
		spin_lock_irqsave(&dc->lock, flags);
		if (last)			dc->state = D_SUBMIT;
		else				dc->state = D_PARTIAL;
		dc->bio_ref++;
		spin_unlock_irqrestore(&dc->lock, flags);

		atomic_inc(&queued_discard);
		dc->queued++;
		list_move_tail(&dc->list, local_wait_list);

		/* sanity check on discard range */
		__check_sit_bitmap(m_sbi, lstart, lstart + len);

		bio->bi_private = dc;
		bio->bi_end_io = f2fs_submit_discard_endio;
		bio->bi_opf |= flag;
//		m_sbi->submit_bio(bio);
		m_sbi->m_io_control->submit_async_io(bio);

		atomic_inc(&issued_discard);

		m_sbi->f2fs_update_iostat(FS_DISCARD, 1);

		lstart += len;
		start += len;
		total_len -= len;
		len = total_len;
	}

	if (!err && len)
	{
		undiscard_blks -= len;
		__update_discard_tree_range(bdev, lstart, start, len);
	}
	return err;
}


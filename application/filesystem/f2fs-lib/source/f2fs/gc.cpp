///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"
// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/gc.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
//#include <linux/fs.h>
//#include <linux/module.h>
//#include <linux/backing-dev.h>
//#include <linux/init.h>
//#include <linux/f2fs_fs.h>
//#include <linux/kthread.h>
//#include <linux/delay.h>
//#include <linux/freezer.h>
//#include <linux/sched/signal.h>
//
#include "../../include/f2fs_fs.h"
#include "node.h"
#include "segment.h"
#include "gc.h"
//#include <trace/events/f2fs.h>

LOCAL_LOGGER_ENABLE(L"f2fs.gc", LOGGER_LEVEL_DEBUGINFO);

//static struct kmem_cache *victim_entry_slab;

static unsigned int count_bits(const unsigned long *addr, unsigned int offset, unsigned int len);

//static int gc_thread_func(void *data)
DWORD f2fs_gc_kthread::gc_thread_func(void)
{
//	struct f2fs_sb_info *sbi = data;
	f2fs_sb_info* sbi = m_sbi;
//	struct f2fs_gc_kthread *gc_th = sbi->gc_thread;
//	wait_queue_head_t *wq = &sbi->gc_thread->gc_wait_queue_head;
//	wait_queue_head_t *fggc_wq = &sbi->gc_thread->fggc_wq;
	unsigned int wait_ms;

	wait_ms = min_sleep_time;

#if 0
	set_freezable();
#endif
	do
	{
		bool sync_mode, foreground = false;

#if 0
		wait_event_interruptible_timeout(*wq,
			kthread_should_stop() || freezing(current) ||
			waitqueue_active(fggc_wq) || gc_wake,
			msecs_to_jiffies(wait_ms));

		if (test_opt(sbi, GC_MERGE) && waitqueue_active(fggc_wq)) foreground = true;
#else
		DWORD ir = WaitForSingleObject(m_trigger, wait_ms);
		if (test_opt(sbi, GC_MERGE) && ir == 0) foreground = true;
#endif
		/* give it a try one time */
		if (gc_wake)	gc_wake = 0;
#if 0
		if (try_to_freeze())
		{
			stat_other_skip_bggc_count(sbi);
			continue;
		}
#endif
		if (kthread_should_stop())			break;

		if (sbi->s_writers.frozen >= SB_FREEZE_WRITE)
		{
			increase_sleep_time(&wait_ms);
			stat_other_skip_bggc_count(sbi);
			continue;
		}

		if (time_to_inject(sbi, FAULT_CHECKPOINT))
		{
			f2fs_show_injection_info(sbi, FAULT_CHECKPOINT);
			f2fs_stop_checkpoint(sbi, false);
		}

		if (!sb_start_write_trylock(sbi))
		{
			stat_other_skip_bggc_count(sbi);
			continue;
		}

		/*
		 * [GC triggering condition]
		 * 0. GC is not conducted currently.
		 * 1. There are enough dirty segments.
		 * 2. IO subsystem is idle by checking the # of writeback pages.
		 * 3. IO subsystem is idle by checking the # of requests in bdev's request list.
		 *
		 * Note) We have to avoid triggering GCs frequently. Because it is possible that some segments can be
		 * invalidated soon after by user update or deletion. So, I'd like to wait some time to collect dirty segments. */
		if (sbi->gc_mode == GC_URGENT_HIGH)
		{
			wait_ms = urgent_sleep_time;
			down_write(&sbi->gc_lock);
			goto do_gc;
		}

		if (foreground)
		{
			down_write(&sbi->gc_lock);
			goto do_gc;
		}
		else if (!down_write_trylock(&sbi->gc_lock))
		{
			stat_other_skip_bggc_count(sbi);
			goto next;
		}

		if (!is_idle(sbi, GC_TIME))
		{
			increase_sleep_time(&wait_ms);
			up_write(&sbi->gc_lock);
			stat_io_skip_bggc_count(sbi);
			goto next;
		}

		if (has_enough_invalid_blocks(sbi))			decrease_sleep_time(&wait_ms);
		else										increase_sleep_time(&wait_ms);
	do_gc:
		if (!foreground)		stat_inc_bggc_count(sbi->stat_info);

		sync_mode = F2FS_OPTION(sbi).bggc_mode == BGGC_MODE_SYNC;

		/* foreground GC was been triggered via f2fs_balance_fs() */
		if (foreground)			sync_mode = false;

		/* if return value is not zero, no victim was selected */
		if (f2fs_gc(sbi, sync_mode, !foreground, false, NULL_SEGNO))		wait_ms = no_gc_sleep_time;

#if 0
		if (foreground) wake_up_all(&fggc_wq);
#endif 
//		trace_f2fs_background_gc(sbi->sb, wait_ms, sbi->prefree_segments(), sbi->free_segments());
		/* balancing f2fs's metadata periodically */
		f2fs_balance_fs_bg(sbi, true);
	next:
		sb_end_write(sbi);

	} while (!kthread_should_stop());
	return 0;
}

int f2fs_start_gc_thread(f2fs_sb_info *sbi)
{
	f2fs_gc_kthread *gc_th;
//	dev_t dev = sbi->s_bdev->bd_dev;
	int err = 0;

	//gc_th = f2fs_kmalloc(sbi, sizeof(struct f2fs_gc_kthread), GFP_KERNEL);
	//if (!gc_th) {
	//	err = -ENOMEM;
	//	goto out;
	//}
	gc_th = new f2fs_gc_kthread(sbi);
	if (!gc_th) THROW_ERROR(ERR_MEM, L"failed on creating f2fs_gc_kthread object");

	//gc_th->urgent_sleep_time = DEF_GC_THREAD_URGENT_SLEEP_TIME;
	//gc_th->min_sleep_time = DEF_GC_THREAD_MIN_SLEEP_TIME;
	//gc_th->max_sleep_time = DEF_GC_THREAD_MAX_SLEEP_TIME;
	//gc_th->no_gc_sleep_time = DEF_GC_THREAD_NOGC_SLEEP_TIME;

	//gc_th->gc_wake = 0;

	sbi->gc_thread = gc_th;
#if 0 //TODO
	init_waitqueue_head(&sbi->gc_thread->gc_wait_queue_head);
	init_waitqueue_head(&sbi->gc_thread->fggc_wq);
#endif 
//	sbi->gc_thread->f2fs_gc_task = kthread_run(gc_thread_func, sbi, "f2fs_gc-%u:%u", MAJOR(dev), MINOR(dev));
	//if (IS_ERR(gc_th->f2fs_gc_task)) {
	//	err = PTR_ERR(gc_th->f2fs_gc_task);
	//	kfree(gc_th);
	//	sbi->gc_thread = NULL;
	//}
	bool br = gc_th->Start(THREAD_PRIORITY_BELOW_NORMAL);
	if (!br) THROW_ERROR(ERR_APP, L"failed to start gc thread");
//out:
	return err;
}

f2fs_gc_kthread::f2fs_gc_kthread(f2fs_sb_info* sbi)
{
	urgent_sleep_time = DEF_GC_THREAD_URGENT_SLEEP_TIME;
	min_sleep_time = DEF_GC_THREAD_MIN_SLEEP_TIME;
	max_sleep_time = DEF_GC_THREAD_MAX_SLEEP_TIME;
	no_gc_sleep_time = DEF_GC_THREAD_NOGC_SLEEP_TIME;
	gc_wake = 0;

	m_sbi = sbi;
//<YUAN> 初始化event
	m_trigger = CreateEvent(NULL, FALSE, TRUE, NULL);
}

f2fs_gc_kthread::~f2fs_gc_kthread(void)
{
	CloseHandle(m_trigger);
}

void f2fs_sb_info::f2fs_stop_gc_thread(void)
{
	LOG_STACK_TRACE();
	f2fs_gc_kthread *gc_th = gc_thread;
	if (!gc_th)	return;
	//kthread_stop(gc_th->f2fs_gc_task);
	//wake_up_all(&gc_th->fggc_wq);
	//kfree(gc_th);
	//sbi->gc_thread = NULL;
	gc_th->Stop();
	delete gc_th;
	gc_thread = nullptr;
}

static int select_gc_type(struct f2fs_sb_info *sbi, int gc_type)
{
	int gc_mode;

	if (gc_type == BG_GC) {
		if (sbi->am.atgc_enabled)
			gc_mode = GC_AT;
		else
			gc_mode = GC_CB;
	} else {
		gc_mode = GC_GREEDY;
	}

	switch (sbi->gc_mode) {
	case GC_IDLE_CB:
		gc_mode = GC_CB;
		break;
	case GC_IDLE_GREEDY:
	case GC_URGENT_HIGH:
		gc_mode = GC_GREEDY;
		break;
	case GC_IDLE_AT:
		gc_mode = GC_AT;
		break;
	}

	return gc_mode;
}

static void select_policy(f2fs_sb_info *sbi, int gc_type, int type, struct victim_sel_policy *p)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);

	if (p->alloc_mode == SSR) {
		p->gc_mode = GC_GREEDY;
		p->dirty_bitmap = dirty_i->dirty_segmap[type];
		p->max_search = dirty_i->nr_dirty[type];
		p->ofs_unit = 1;
	} else if (p->alloc_mode == AT_SSR) {
		p->gc_mode = GC_GREEDY;
		p->dirty_bitmap = dirty_i->dirty_segmap[type];
		p->max_search = dirty_i->nr_dirty[type];
		p->ofs_unit = 1;
	} else {
		p->gc_mode = select_gc_type(sbi, gc_type);
		p->ofs_unit = sbi->segs_per_sec;
		if (sbi->__is_large_section()) {
			p->dirty_bitmap = dirty_i->dirty_secmap;
			p->max_search = count_bits(p->dirty_bitmap,
						0, sbi->MAIN_SECS());
		} else {
			p->dirty_bitmap = dirty_i->dirty_segmap[DIRTY];
			p->max_search = dirty_i->nr_dirty[DIRTY];
		}
	}

	/*
	 * adjust candidates range, should select all dirty segments for
	 * foreground GC and urgent GC cases.
	 */
	if (gc_type != FG_GC &&
			(sbi->gc_mode != GC_URGENT_HIGH) &&
			(p->gc_mode != GC_AT && p->alloc_mode != AT_SSR) &&
			p->max_search > sbi->max_victim_search)
		p->max_search = sbi->max_victim_search;

	/* let's select beginning hot/small space first in no_heap mode*/
	if (test_opt(sbi, NOHEAP) &&
		(type == CURSEG_HOT_DATA || IS_NODESEG(type)))
		p->offset = 0;
	else
		p->offset = sbi->SIT_I()->last_victim[p->gc_mode];
}


static unsigned int get_max_cost(f2fs_sb_info *sbi, victim_sel_policy *p)
{
	/* SSR allocates in a segment unit */
	if (p->alloc_mode == SSR)			return sbi->blocks_per_seg;
	else if (p->alloc_mode == AT_SSR)	return UINT_MAX;

	/* LFS */
	if (p->gc_mode == GC_GREEDY)
		return 2 * sbi->blocks_per_seg * p->ofs_unit;
	else if (p->gc_mode == GC_CB)
		return UINT_MAX;
	else if (p->gc_mode == GC_AT)
		return UINT_MAX;
	else /* No other gc_mode */
		return 0;
}


static unsigned int check_bg_victims(f2fs_sb_info *sbi)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned int secno;

	/* If the gc_type is FG_GC, we can select victim segments selected by background GC before.
	 * Those segments guarantee they have small valid blocks. */
	for_each_set_bit(secno, dirty_i->victim_secmap, sbi->MAIN_SECS()) 
	{
		if (sec_usage_check(sbi, secno))		continue;
		__clear_bit(secno, dirty_i->victim_secmap);
		return GET_SEG_FROM_SEC(sbi, secno);
	}
	return NULL_SEGNO;
}

static unsigned int get_cb_cost(struct f2fs_sb_info *sbi, unsigned int segno)
{
	struct sit_info *sit_i = sbi->SIT_I();
	unsigned int secno = GET_SEC_FROM_SEG(sbi, segno);
	unsigned int start = GET_SEG_FROM_SEC(sbi, secno);
	unsigned long long mtime = 0;
	unsigned int vblocks;
	unsigned char age = 0;
	unsigned char u;
	unsigned int i;
	unsigned int usable_segs_per_sec = f2fs_usable_segs_in_sec(sbi, segno);

	for (i = 0; i < usable_segs_per_sec; i++)
		mtime += sbi->get_seg_entry( start + i)->mtime;
	vblocks = get_valid_blocks(sbi, segno, true);

//	mtime = div_u64(mtime, usable_segs_per_sec);
	mtime = mtime / usable_segs_per_sec;
//	vblocks = div_u64(vblocks, usable_segs_per_sec);
	vblocks = vblocks / usable_segs_per_sec;

	u = (vblocks * 100) >> sbi->log_blocks_per_seg;

	/* Handle if the system time has changed by the user */
	if (mtime < sit_i->min_mtime)		sit_i->min_mtime = mtime;
	if (mtime > sit_i->max_mtime)		sit_i->max_mtime = mtime;
	if (sit_i->max_mtime != sit_i->min_mtime)
//		age = 100 - div64_u64(100 * (mtime - sit_i->min_mtime), sit_i->max_mtime - sit_i->min_mtime);
		age = 100 - (100 * (mtime - sit_i->min_mtime))/ (sit_i->max_mtime - sit_i->min_mtime);

	return UINT_MAX - ((100 * (100 - u) * age) / (100 + u));
}

static inline unsigned int get_gc_cost(f2fs_sb_info *sbi, unsigned int segno, struct victim_sel_policy *p)
{
	if (p->alloc_mode == SSR)
		return sbi->get_seg_entry( segno)->ckpt_valid_blocks;

	/* alloc_mode == LFS */
	if (p->gc_mode == GC_GREEDY)
		return get_valid_blocks(sbi, segno, true);
	else if (p->gc_mode == GC_CB)
		return get_cb_cost(sbi, segno);

	f2fs_bug_on(sbi, 1);
	return 0;
}

static unsigned int count_bits(const unsigned long *addr, unsigned int offset, unsigned int len)
{
	unsigned int end = offset + len, sum = 0;

	while (offset < end) 
	{
		if (__test_bit(offset++, addr))		++sum;
	}
	return sum;
}

static struct victim_entry *attach_victim_entry(f2fs_sb_info *sbi,
				unsigned long long mtime, unsigned int segno,
				rb_node *parent, rb_node **p, bool left_most)
{
	atgc_management *am = &sbi->am;
	victim_entry *ve;

	ve =  f2fs_kmem_cache_alloc<victim_entry>(/*victim_entry_slab*/ NULL, GFP_NOFS);

	ve->mtime = mtime;
	ve->segno = segno;

	rb_link_node(&ve->rb_node, parent, p);
	rb_insert_color_cached(&ve->rb_node, &am->root, left_most);

	list_add_tail(&ve->list, &am->victim_list);

	am->victim_count++;

	return ve;
}

static void insert_victim_entry(f2fs_sb_info *sbi, unsigned long long mtime, unsigned int segno)
{
	struct atgc_management *am = &sbi->am;
	struct rb_node **p;
	struct rb_node *parent = NULL;
	bool left_most = true;

	p = f2fs_lookup_rb_tree_ext(sbi, &am->root, &parent, mtime, &left_most);
	attach_victim_entry(sbi, mtime, segno, parent, p, left_most);
}


static void add_victim_entry(f2fs_sb_info *sbi, victim_sel_policy *p, unsigned int segno)
{
	struct sit_info *sit_i = sbi->SIT_I();
	unsigned int secno = GET_SEC_FROM_SEG(sbi, segno);
	unsigned int start = GET_SEG_FROM_SEC(sbi, secno);
	unsigned long long mtime = 0;
	unsigned int i;

	if (unlikely(sbi->is_sbi_flag_set(SBI_CP_DISABLED))) 
	{
		if (p->gc_mode == GC_AT && get_valid_blocks(sbi, segno, true) == 0)
			return;
	}

	for (i = 0; i < sbi->segs_per_sec; i++) mtime += sbi->get_seg_entry( start + i)->mtime;
	mtime = mtime / sbi->segs_per_sec;
//	mtime = div_u64(mtime, sbi->segs_per_sec);

	/* Handle if the system time has changed by the user */
	if (mtime < sit_i->min_mtime)
		sit_i->min_mtime = mtime;
	if (mtime > sit_i->max_mtime)
		sit_i->max_mtime = mtime;
	if (mtime < sit_i->dirty_min_mtime)
		sit_i->dirty_min_mtime = mtime;
	if (mtime > sit_i->dirty_max_mtime)
		sit_i->dirty_max_mtime = mtime;

	/* don't choose young section as candidate */
	if (sit_i->dirty_max_mtime - mtime < p->age_threshold)
		return;

	insert_victim_entry(sbi, mtime, segno);
}

static struct rb_node *lookup_central_victim(f2fs_sb_info *sbi, victim_sel_policy *p)
{
	struct atgc_management *am = &sbi->am;
	struct rb_node *parent = NULL;
	bool left_most;

	f2fs_lookup_rb_tree_ext(sbi, &am->root, &parent, p->age, &left_most);

	return parent;
}

static void atgc_lookup_victim(f2fs_sb_info *sbi, victim_sel_policy *p)
{
	struct sit_info *sit_i = sbi->SIT_I();
	struct atgc_management *am = &sbi->am;
	struct rb_root_cached *root = &am->root;
	struct rb_node *node;
	struct rb_entry *re;
	struct victim_entry *ve;
	unsigned long long total_time;
	unsigned long long age, u, accu;
	unsigned long long max_mtime = sit_i->dirty_max_mtime;
	unsigned long long min_mtime = sit_i->dirty_min_mtime;
	unsigned int sec_blocks = BLKS_PER_SEC(sbi);
	unsigned int vblocks;
	unsigned int dirty_threshold = max(am->max_candidate_count,
					am->candidate_ratio *
					am->victim_count / 100);
	unsigned int age_weight = am->age_weight;
	unsigned int cost;
	unsigned int iter = 0;

	if (max_mtime < min_mtime)
		return;

	max_mtime += 1;
	total_time = max_mtime - min_mtime;

//	accu = div64_u64(ULLONG_MAX, total_time);
	accu = ULLONG_MAX / total_time;
//	accu = min_t(unsigned long long, div_u64(accu, 100), DEFAULT_ACCURACY_CLASS);
	accu = min( (unsigned long long)( accu/ 100), (unsigned long long)DEFAULT_ACCURACY_CLASS);

	node = rb_first_cached(root);
next:
	re = rb_entry_safe(node, struct rb_entry, rb_node);
	if (!re)
		return;

	ve = (struct victim_entry *)re;

	if (ve->mtime >= max_mtime || ve->mtime < min_mtime)
		goto skip;

	/* age = 10000 * x% * 60 */
//	age = div64_u64(accu * (max_mtime - ve->mtime), total_time) * age_weight;
	age = (accu * (max_mtime - ve->mtime))/ (total_time) * age_weight;

	vblocks = get_valid_blocks(sbi, ve->segno, true);
	f2fs_bug_on(sbi, !vblocks || vblocks == sec_blocks);

	/* u = 10000 * x% * 40 */
//	u = div64_u64(accu * (sec_blocks - vblocks), sec_blocks) * (100 - age_weight);
	u = (accu * (sec_blocks - vblocks))/( sec_blocks) * (100 - age_weight);

	f2fs_bug_on(sbi, age + u >= UINT_MAX);

	cost = UINT_MAX - (age + u);
	iter++;

	if (cost < p->min_cost ||
			(cost == p->min_cost && age > p->oldest_age)) {
		p->min_cost = cost;
		p->oldest_age = age;
		p->min_segno = ve->segno;
	}
skip:
	if (iter < dirty_threshold) {
		node = rb_next(node);
		goto next;
	}
}


/*
 * select candidates around source section in range of
 * [target - dirty_threshold, target + dirty_threshold]
 */
static void atssr_lookup_victim(f2fs_sb_info *sbi, victim_sel_policy *p)
{
	struct sit_info *sit_i = sbi->SIT_I();
	struct atgc_management *am = &sbi->am;
	struct rb_node *node;
	struct rb_entry *re;
	struct victim_entry *ve;
	unsigned long long age;
	unsigned long long max_mtime = sit_i->dirty_max_mtime;
	unsigned long long min_mtime = sit_i->dirty_min_mtime;
	unsigned int seg_blocks = sbi->blocks_per_seg;
	unsigned int vblocks;
	unsigned int dirty_threshold = max(am->max_candidate_count,
					am->candidate_ratio *
					am->victim_count / 100);
	unsigned int cost;
	unsigned int iter = 0;
	int stage = 0;

	if (max_mtime < min_mtime)
		return;
	max_mtime += 1;
next_stage:
	node = lookup_central_victim(sbi, p);
next_node:
	re = rb_entry_safe(node, struct rb_entry, rb_node);
	if (!re) {
		if (stage == 0)
			goto skip_stage;
		return;
	}

	ve = (struct victim_entry *)re;

	if (ve->mtime >= max_mtime || ve->mtime < min_mtime)
		goto skip_node;

	age = max_mtime - ve->mtime;

	vblocks = sbi->get_seg_entry( ve->segno)->ckpt_valid_blocks;
	f2fs_bug_on(sbi, !vblocks);

	/* rare case */
	if (vblocks == seg_blocks)
		goto skip_node;

	iter++;

	age = max_mtime - abs((const __int64)(p->age - age));
	cost = UINT_MAX - vblocks;

	if (cost < p->min_cost ||
			(cost == p->min_cost && age > p->oldest_age)) {
		p->min_cost = cost;
		p->oldest_age = age;
		p->min_segno = ve->segno;
	}
skip_node:
	if (iter < dirty_threshold) {
		if (stage == 0)
			node = rb_prev(node);
		else if (stage == 1)
			node = rb_next(node);
		goto next_node;
	}
skip_stage:
	if (stage < 1) {
		stage++;
		iter = 0;
		goto next_stage;
	}
}


static void lookup_victim_by_age(f2fs_sb_info *sbi, victim_sel_policy *p)
{
	f2fs_bug_on(sbi, !f2fs_check_rb_tree_consistence(sbi, &sbi->am.root, true));

	if (p->gc_mode == GC_AT)			atgc_lookup_victim(sbi, p);
	else if (p->alloc_mode == AT_SSR)	atssr_lookup_victim(sbi, p);
	else
		f2fs_bug_on(sbi, 1);
}

static void release_victim_entry(f2fs_sb_info *sbi)
{
	struct atgc_management *am = &sbi->am;
	struct victim_entry *ve, *tmp;

	list_for_each_entry_safe(victim_entry, ve, tmp, &am->victim_list, list) 
	{
		list_del(&ve->list);
		kmem_cache_free(/*victim_entry_slab*/NULL, ve);
		am->victim_count--;
	}

//	am->root = RB_ROOT_CACHED;
	am->root.rb_root.rb_node = NULL, am->root.rb_leftmost = NULL;

	f2fs_bug_on(sbi, am->victim_count);
	f2fs_bug_on(sbi, !list_empty(&am->victim_list));
}

/* This function is called from two paths. One is garbage collection and the other is SSR segment selection. When it is called during GC, it just gets a victim segment and it does not remove it from dirty seglist. When it is called from SSR segment selection, it finds a segment which has minimum valid blocks and removes it from dirty seglist. */
// 作为函数指针传递，不能转换成成员函数。需要虚拟化。
static int get_victim_by_default(f2fs_sb_info *sbi, unsigned int *result, int gc_type, int type, char alloc_mode, unsigned long long age)
{
	dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	sit_info *sm = sbi->SIT_I();
	victim_sel_policy p;
	unsigned int secno, last_victim;
	unsigned int last_segment;
	unsigned int nsearched;
	bool is_atgc;
	int ret = 0;

	mutex_lock(&dirty_i->seglist_lock);
	last_segment = sbi->MAIN_SECS() * sbi->segs_per_sec;

	p.alloc_mode = alloc_mode;
	p.age = age;
	p.age_threshold = sbi->am.age_threshold;

retry:
	select_policy(sbi, gc_type, type, &p);
	p.min_segno = NULL_SEGNO;
	p.oldest_age = 0;
	p.min_cost = get_max_cost(sbi, &p);

	is_atgc = (p.gc_mode == GC_AT || p.alloc_mode == AT_SSR);
	nsearched = 0;

	if (is_atgc)
		sbi->SIT_I()->dirty_min_mtime = ULLONG_MAX;

	if (*result != NULL_SEGNO) {
		if (!get_valid_blocks(sbi, *result, false)) {
			ret = -ENODATA;
			goto out;
		}

		if (sec_usage_check(sbi, GET_SEC_FROM_SEG(sbi, *result)))
			ret = -EBUSY;
		else
			p.min_segno = *result;
		goto out;
	}

	ret = -ENODATA;
	if (p.max_search == 0)
		goto out;

	if (sbi->__is_large_section() && p.alloc_mode == LFS) {
		if (sbi->next_victim_seg[BG_GC] != NULL_SEGNO) {
			p.min_segno = sbi->next_victim_seg[BG_GC];
			*result = p.min_segno;
			sbi->next_victim_seg[BG_GC] = NULL_SEGNO;
			goto got_result;
		}
		if (gc_type == FG_GC &&
				sbi->next_victim_seg[FG_GC] != NULL_SEGNO) {
			p.min_segno = sbi->next_victim_seg[FG_GC];
			*result = p.min_segno;
			sbi->next_victim_seg[FG_GC] = NULL_SEGNO;
			goto got_result;
		}
	}

	last_victim = sm->last_victim[p.gc_mode];
	if (p.alloc_mode == LFS && gc_type == FG_GC) {
		p.min_segno = check_bg_victims(sbi);
		if (p.min_segno != NULL_SEGNO)
			goto got_it;
	}

	while (1) {
		unsigned long cost, *dirty_bitmap;
		unsigned int unit_no, segno;

		dirty_bitmap = p.dirty_bitmap;
		unit_no = find_next_bit(dirty_bitmap, last_segment / p.ofs_unit, p.offset / p.ofs_unit);
		segno = unit_no * p.ofs_unit;
		if (segno >= last_segment) 
		{
			if (sm->last_victim[p.gc_mode]) 
			{
				last_segment = sm->last_victim[p.gc_mode];
				sm->last_victim[p.gc_mode] = 0;
				p.offset = 0;
				continue;
			}
			break;
		}

		p.offset = segno + p.ofs_unit;
		nsearched++;

#ifdef CONFIG_F2FS_CHECK_FS
		/*
		 * skip selecting the invalid segno (that is failed due to block
		 * validity check failure during GC) to avoid endless GC loop in
		 * such cases.
		 */
		if (test_bit(segno, sm->invalid_segmap))
			goto next;
#endif

		secno = GET_SEC_FROM_SEG(sbi, segno);

		if (sec_usage_check(sbi, secno))
			goto next;

		/* Don't touch checkpointed data */
		if (unlikely(sbi->is_sbi_flag_set( SBI_CP_DISABLED))) 
		{
			if (p.alloc_mode == LFS) 
			{	/* LFS is set to find source section during GC. The victim should have no checkpointed data. */
				if (get_ckpt_valid_blocks(sbi, segno, true))	goto next;
			} 
			else 
			{	/* SSR | AT_SSR are set to find target segment for writes which can be full by checkpointed and newly written blocks.	 */
				if (!f2fs_segment_has_free_slot(sbi, segno))	goto next;
			}
		}

		if (gc_type == BG_GC && __test_bit(secno, dirty_i->victim_secmap))
			goto next;

		if (is_atgc) {
			add_victim_entry(sbi, &p, segno);
			goto next;
		}

		cost = get_gc_cost(sbi, segno, &p);

		if (p.min_cost > cost) {
			p.min_segno = segno;
			p.min_cost = cost;
		}
next:
		if (nsearched >= p.max_search) {
			if (!sm->last_victim[p.gc_mode] && segno <= last_victim)
				sm->last_victim[p.gc_mode] =
					last_victim + p.ofs_unit;
			else
				sm->last_victim[p.gc_mode] = segno + p.ofs_unit;
			sm->last_victim[p.gc_mode] %=
				(sbi->MAIN_SECS() * sbi->segs_per_sec);
			break;
		}
	}

	/* get victim for GC_AT/AT_SSR */
	if (is_atgc) {
		lookup_victim_by_age(sbi, &p);
		release_victim_entry(sbi);
	}

	if (is_atgc && p.min_segno == NULL_SEGNO &&
			sm->elapsed_time < p.age_threshold) {
		p.age_threshold = 0;
		goto retry;
	}

	if (p.min_segno != NULL_SEGNO) 
	{
got_it:
		*result = (p.min_segno / p.ofs_unit) * p.ofs_unit;
got_result:
		if (p.alloc_mode == LFS) 
		{
			secno = GET_SEC_FROM_SEG(sbi, p.min_segno);
			if (gc_type == FG_GC)	sbi->cur_victim_sec = secno;
			else					__set_bit(secno, dirty_i->victim_secmap);
		}
		ret = 0;
	}
out:
	//if (p.min_segno != NULL_SEGNO)
	//	trace_f2fs_get_victim(sbi->sb, type, gc_type, &p, sbi->cur_victim_sec,	sbi->prefree_segments(), sbi->free_segments());
	mutex_unlock(&dirty_i->seglist_lock);

	return ret;
}

static const struct victim_selection default_v_ops = { get_victim_by_default };

static inode *find_gc_inode(gc_inode_list *gc_list, nid_t ino)
{
	inode_entry *ie;
	ie = radix_tree_lookup<inode_entry>(&gc_list->iroot, ino);
	if (ie) return ie->inode;
	return NULL;
}

static void add_gc_inode(gc_inode_list *gc_list, inode *inode)
{
	inode_entry *new_ie;

	if (inode == find_gc_inode(gc_list, inode->i_ino)) 
	{
		iput(inode);
		return;
	}
	new_ie = f2fs_kmem_cache_alloc<inode_entry>(f2fs_inode_entry_slab, GFP_NOFS);
	new_ie->inode = inode;

	f2fs_radix_tree_insert(&gc_list->iroot, inode->i_ino, new_ie);
	list_add_tail(&new_ie->list, &gc_list->ilist);
}

static void put_gc_inode(gc_inode_list *gc_list)
{
	inode_entry *ie, *next_ie;

	list_for_each_entry_safe(inode_entry, ie, next_ie, &gc_list->ilist, list)
	{
		radix_tree_delete<inode_entry>(&gc_list->iroot, ie->inode->i_ino);
		iput(ie->inode);
		list_del(&ie->list);
		kmem_cache_free(f2fs_inode_entry_slab, ie);
	}
}

static int check_valid_map(f2fs_sb_info *sbi, unsigned int segno, int offset)
{
	sit_info *sit_i = sbi->SIT_I();
	seg_entry *sentry;
	int ret;

	down_read(&sit_i->sentry_lock);
	sentry = sbi->get_seg_entry( segno);
	ret = f2fs_test_bit(offset, sentry->cur_valid_map);
	up_read(&sit_i->sentry_lock);
	return ret;
}

/*
 * This function compares node address got in summary with that in NAT.
 * On validity, copy that node with cold status, otherwise (invalid node) ignore that. */
static int gc_node_segment(f2fs_sb_info *sbi, f2fs_summary *sum, unsigned int segno, int gc_type)
{
	f2fs_summary *entry;
	block_t start_addr;
	int off;
	int phase = 0;
	bool fggc = (gc_type == FG_GC);
	int submitted = 0;
	unsigned int usable_blks_in_seg = f2fs_usable_blks_in_seg(sbi, segno);

	start_addr = START_BLOCK(sbi, segno);

next_step:
	entry = sum;

	if (fggc && phase == 2)
		atomic_inc(&sbi->wb_sync_req[NODE]);

	for (off = 0; off < usable_blks_in_seg; off++, entry++) {
		nid_t nid = le32_to_cpu(entry->nid);
		struct page *node_page;
		struct node_info ni;
		int err;

		/* stop BG_GC if there is not enough free sections. */
		if (gc_type == BG_GC && sbi->has_not_enough_free_secs( 0, 0))
			return submitted;

		if (check_valid_map(sbi, segno, off) == 0)
			continue;

		if (phase == 0) 
		{
			sbi->f2fs_ra_meta_pages( NAT_BLOCK_OFFSET(nid), 1, META_NAT, true);
			continue;
		}

		if (phase == 1) 
		{
			f2fs_ra_node_page(sbi, nid);
			continue;
		}

		/* phase == 2 */
		node_page = sbi->f2fs_get_node_page( nid);
		if (IS_ERR(node_page))
			continue;

		/* block may become invalid during f2fs_get_node_page */
		if (check_valid_map(sbi, segno, off) == 0) 
		{
			f2fs_put_page(node_page, 1);
			continue;
		}

		if (NM_I(sbi)->f2fs_get_node_info( nid, &ni)) 
		{
			f2fs_put_page(node_page, 1);
			continue;
		}

		if (ni.blk_addr != start_addr + off) 
		{
			f2fs_put_page(node_page, 1);
			continue;
		}

		err = f2fs_move_node_page(node_page, gc_type);
		if (!err && gc_type == FG_GC)		submitted++;
		stat_inc_node_blk_count(sbi, 1, gc_type);
	}

	if (++phase < 3)		goto next_step;

	if (fggc)		atomic_dec(&sbi->wb_sync_req[NODE]);
	return submitted;
}

/* Calculate start block index indicating the given node offset.
 * Be careful, caller should give this node offset only indicating direct node blocks. If any node offsets, which point the other types of node blocks such as indirect or double indirect node blocks, are given, it must be a caller's bug. */
block_t f2fs_start_bidx_of_node(unsigned int node_ofs, f2fs_inode_info *inode)
{
	unsigned int indirect_blks = 2 * NIDS_PER_BLOCK + 4;
	unsigned int bidx;

	if (node_ofs == 0)
		return 0;

	if (node_ofs <= 2) {
		bidx = node_ofs - 1;
	} else if (node_ofs <= indirect_blks) {
		int dec = (node_ofs - 4) / (NIDS_PER_BLOCK + 1);

		bidx = node_ofs - 2 - dec;
	} else {
		int dec = (node_ofs - indirect_blks - 3) / (NIDS_PER_BLOCK + 1);

		bidx = node_ofs - 5 - dec;
	}
	return bidx * ADDRS_PER_BLOCK(inode) + ADDRS_PER_INODE(inode);
}

static bool is_alive(f2fs_sb_info *sbi, f2fs_summary *sum, node_info *dni, block_t blkaddr, unsigned int *nofs)
{
	struct page *node_page;
	nid_t nid;
	unsigned int ofs_in_node;
	block_t source_blkaddr;

	nid = le32_to_cpu(sum->nid);
	ofs_in_node = le16_to_cpu(sum->_u._s.ofs_in_node);

	node_page = sbi->f2fs_get_node_page( nid);
	if (IS_ERR(node_page))
		return false;

	if (NM_I(sbi)->f2fs_get_node_info( nid, dni)) {
		f2fs_put_page(node_page, 1);
		return false;
	}

	if (sum->_u._s.version != dni->version)
	{
		LOG_WARNING(L"[err] valid data with mismatched node version.");
		sbi->set_sbi_flag(SBI_NEED_FSCK);
	}

	*nofs = ofs_of_node(node_page);
	source_blkaddr = data_blkaddr(NULL, node_page, ofs_in_node);
	f2fs_put_page(node_page, 1);

	if (source_blkaddr != blkaddr) {
#ifdef CONFIG_F2FS_CHECK_FS
		unsigned int segno = GET_SEGNO(sbi, blkaddr);
		unsigned long offset = GET_BLKOFF_FROM_SEG0(sbi, blkaddr);

		if (unlikely(check_valid_map(sbi, segno, offset))) {
			if (!test_and_set_bit(segno, sbi->SIT_I()->invalid_segmap)) {
				f2fs_err(sbi, "mismatched blkaddr %u (source_blkaddr %u) in seg %u\n",
						blkaddr, source_blkaddr, segno);
				f2fs_bug_on(sbi, 1);
			}
		}
#endif
		return false;
	}
	return true;
}

//static int ra_data_block(inode *inode, pgoff_t index)
int f2fs_inode_info::ra_data_block(pgoff_t index)
{
	f2fs_sb_info *sbi = F2FS_I_SB(this);
//	address_space *mapping = inode->i_mapping;
	dnode_of_data dn;
	struct page *page;
	extent_info ei = {0, 0, 0};

	f2fs_io_info fio;
	fio.sbi = sbi;
	fio.ino = i_ino;
	fio.type = DATA;
	fio.temp = COLD;
	fio.op = REQ_OP_READ;
	fio.op_flags = 0;
	fio.encrypted_page = NULL;
	fio.in_list = false;
	fio.retry = false;
	int err;

	page = f2fs_grab_cache_page(i_mapping, index, true);
	if (!page) return -ENOMEM;

	if (f2fs_lookup_extent_cache(this, index, &ei))
	{
		dn.data_blkaddr = ei.blk + index - ei.fofs;
		if (unlikely(!sbi->f2fs_is_valid_blkaddr(dn.data_blkaddr, DATA_GENERIC_ENHANCE_READ)))
		{
			err = -EFSCORRUPTED;
			goto put_page;
		}
		goto got_it;
	}

	dn.set_new_dnode(this, NULL, NULL, 0);
	err = f2fs_get_dnode_of_data(&dn, index, LOOKUP_NODE);
	if (err) 	goto put_page;
	f2fs_put_dnode(&dn);

	if (!__is_valid_data_blkaddr(dn.data_blkaddr)) {
		err = -ENOENT;
		goto put_page;
	}
	if (unlikely(!sbi->f2fs_is_valid_blkaddr(dn.data_blkaddr,
						DATA_GENERIC_ENHANCE))) {
		err = -EFSCORRUPTED;
		goto put_page;
	}
got_it:
	/* read page */
	fio.page = page;
	fio.new_blkaddr = fio.old_blkaddr = dn.data_blkaddr;

	/* don't cache encrypted data into meta inode until previous dirty data were writebacked to avoid racing between GC and flush. */
	f2fs_wait_on_page_writeback(page, DATA, true, true);

	f2fs_wait_on_block_writeback(this, dn.data_blkaddr);

	fio.encrypted_page = f2fs_pagecache_get_page(META_MAPPING(sbi),	dn.data_blkaddr, FGP_LOCK | FGP_CREAT, GFP_NOFS);
	if (!fio.encrypted_page) 
	{
		err = -ENOMEM;
		goto put_page;
	}

	err = sbi->f2fs_submit_page_bio(&fio);
	if (err) goto put_encrypted_page;
	f2fs_put_page(fio.encrypted_page, 0);
	f2fs_put_page(page, 1);

	f2fs_update_iostat(sbi, FS_DATA_READ_IO, F2FS_BLKSIZE);
	f2fs_update_iostat(sbi, FS_GDATA_READ_IO, F2FS_BLKSIZE);

	return 0;
put_encrypted_page:
	f2fs_put_page(fio.encrypted_page, 1);
put_page:
	f2fs_put_page(page, 1);
	return err;
}

int f2fs_pin_file_control(struct inode* inode, bool inc) UNSUPPORT_1(int);


/* Move data block via META_MAPPING while keeping locked data page. This can be used to move blocks, aka LBAs, directly
   on disk. */
//static int move_data_block(inode *inode, block_t bidx, int gc_type, unsigned int segno, int off)
int f2fs_inode_info::move_data_block(block_t bidx, int gc_type, unsigned int segno, int off)
{
	f2fs_io_info fio;
	fio.sbi = F2FS_I_SB(this);
	fio.ino = i_ino;
	fio.type = DATA;
	fio.temp = COLD;
	fio.op = REQ_OP_READ;
	fio.op_flags = 0;
	fio.encrypted_page = NULL;
	fio.in_list = false;
	fio.retry = false;

	struct dnode_of_data dn;
	struct f2fs_summary sum;
	struct node_info ni;
	struct page *page, *mpage;
	block_t newaddr;
	int err = 0;
	bool lfs_mode = f2fs_lfs_mode(fio.sbi);
	int type = fio.sbi->am.atgc_enabled && (gc_type == BG_GC) &&
				(fio.sbi->gc_mode != GC_URGENT_HIGH) ?
				CURSEG_ALL_DATA_ATGC : CURSEG_COLD_DATA;

	/* do not read out */
	page = f2fs_grab_cache_page(i_mapping, bidx, false);
	if (!page)
		return -ENOMEM;

	if (!check_valid_map(F2FS_I_SB(this), segno, off)) {
		err = -ENOENT;
		goto out;
	}

	if (f2fs_is_atomic_file(this)) 
	{
		i_gc_failures[GC_FAILURE_ATOMIC]++;
		F2FS_I_SB(this)->skipped_atomic_files[gc_type]++;
		err = -EAGAIN;
		goto out;
	}

	if (f2fs_is_pinned_file(this)) 
	{
		f2fs_pin_file_control(this, true);
		err = -EAGAIN;
		goto out;
	}

	dn.set_new_dnode(this, NULL, NULL, 0);
	err = f2fs_get_dnode_of_data(&dn, bidx, LOOKUP_NODE);
	if (err)
		goto out;

	if (unlikely(dn.data_blkaddr == NULL_ADDR)) {
		ClearPageUptodate(page);
		err = -ENOENT;
		goto put_out;
	}

	/* don't cache encrypted data into meta inode until previous dirty data were writebacked to avoid racing between GC and flush. */
	f2fs_wait_on_page_writeback(page, DATA, true, true);

	f2fs_wait_on_block_writeback(this, dn.data_blkaddr);

	err = NM_I(fio.sbi)->f2fs_get_node_info(dn.nid, &ni);
	if (err)
		goto put_out;

	/* read page */
	fio.page = page;
	fio.new_blkaddr = fio.old_blkaddr = dn.data_blkaddr;

	if (lfs_mode)
		down_write(&fio.sbi->io_order_lock);

	mpage = f2fs_grab_cache_page(META_MAPPING(fio.sbi),
					fio.old_blkaddr, false);
	if (!mpage) {
		err = -ENOMEM;
		goto up_out;
	}

	fio.encrypted_page = mpage;

	/* read source block in mpage */
	if (!PageUptodate(mpage)) {
		err = fio.sbi->f2fs_submit_page_bio(&fio);
		if (err) {
			f2fs_put_page(mpage, 1);
			goto up_out;
		}

		f2fs_update_iostat(fio.sbi, FS_DATA_READ_IO, F2FS_BLKSIZE);
		f2fs_update_iostat(fio.sbi, FS_GDATA_READ_IO, F2FS_BLKSIZE);

		lock_page(mpage);
		if (unlikely(mpage->mapping != META_MAPPING(fio.sbi) ||
						!PageUptodate(mpage))) {
			err = -EIO;
			f2fs_put_page(mpage, 1);
			goto up_out;
		}
	}

	set_summary(&sum, dn.nid, dn.ofs_in_node, ni.version);

	/* allocate block address */
	f2fs_allocate_data_block(fio.sbi, NULL, fio.old_blkaddr, &newaddr,
				&sum, type, NULL);

	fio.encrypted_page = f2fs_pagecache_get_page(META_MAPPING(fio.sbi),
				newaddr, FGP_LOCK | FGP_CREAT, GFP_NOFS);
	if (!fio.encrypted_page) {
		err = -ENOMEM;
		f2fs_put_page(mpage, 1);
		goto recover_block;
	}

	/* write target block */
	f2fs_wait_on_page_writeback(fio.encrypted_page, DATA, true, true);
	memcpy(page_address<void>(fio.encrypted_page), page_address<void>(mpage), PAGE_SIZE);
	f2fs_put_page(mpage, 1);
	invalidate_mapping_pages(META_MAPPING(fio.sbi), fio.old_blkaddr, fio.old_blkaddr);

	set_page_dirty(fio.encrypted_page);
	// clear_page_dirty_for_io 用于强制回写
	if (clear_page_dirty_for_io(fio.encrypted_page))	fio.sbi->dec_page_count( F2FS_DIRTY_META);
	set_page_writeback(fio.encrypted_page);
	ClearPageError(page);

	fio.op = REQ_OP_WRITE;
	fio.op_flags = REQ_SYNC;
	fio.new_blkaddr = newaddr;
	m_sbi->f2fs_submit_page_write(&fio);
	if (fio.retry)
	{
		err = -EAGAIN;
		if (PageWriteback(fio.encrypted_page))
		{
#if 0
			end_page_writeback(fio.encrypted_page)
#endif
		};
		goto put_page_out;
	}

	f2fs_update_iostat(fio.sbi, FS_GC_DATA_IO, F2FS_BLKSIZE);

	f2fs_update_data_blkaddr(&dn, newaddr);
	set_inode_flag(FI_APPEND_WRITE);
	if (page->index == 0)
		set_inode_flag(FI_FIRST_BLOCK_WRITTEN);
put_page_out:
	f2fs_put_page(fio.encrypted_page, 1);
recover_block:
	if (err) fio.sbi->f2fs_do_replace_block(&sum, newaddr, fio.old_blkaddr, true, true, true);
up_out:
	if (lfs_mode)
		up_write(&fio.sbi->io_order_lock);
put_out:
	f2fs_put_dnode(&dn);
out:
	f2fs_put_page(page, 1);
	return err;
}

static int move_data_page(f2fs_inode_info *inode, block_t bidx, int gc_type, unsigned int segno, int off)
{
	page *ppage;
	int err = 0;

	ppage = inode->f2fs_get_lock_data_page(bidx, true);
	if (IS_ERR(ppage))
		return PTR_ERR(ppage);

	if (!check_valid_map(F2FS_I_SB(inode), segno, off)) {
		err = -ENOENT;
		goto out;
	}

	if (f2fs_is_atomic_file(inode)) {
		F2FS_I(inode)->i_gc_failures[GC_FAILURE_ATOMIC]++;
		F2FS_I_SB(inode)->skipped_atomic_files[gc_type]++;
		err = -EAGAIN;
		goto out;
	}
	if (f2fs_is_pinned_file(inode)) {
		if (gc_type == FG_GC)
			f2fs_pin_file_control(inode, true);
		err = -EAGAIN;
		goto out;
	}

	if (gc_type == BG_GC) 
	{
		if (PageWriteback(ppage)) {
			err = -EAGAIN;
			goto out;
		}
		set_page_dirty(ppage);
		set_cold_data(ppage);
	} 
	else 
	{
		f2fs_io_info fio;
		fio.sbi = F2FS_I_SB(inode);
		fio.ino = inode->i_ino;
		fio.type = DATA;
		fio.temp = COLD;
		fio.op = REQ_OP_WRITE;
		fio.op_flags = REQ_SYNC;
		fio.old_blkaddr = NULL_ADDR;
		fio.page = ppage;
		fio.encrypted_page = NULL;
		fio.need_lock = LOCK_REQ;
		fio.io_type = FS_GC_DATA_IO;

		bool is_dirty = PageDirty(ppage);

retry:
		f2fs_wait_on_page_writeback(ppage, DATA, true, true);

		set_page_dirty(ppage);
		if (clear_page_dirty_for_io(ppage)) 
		{
			F_LOG_DEBUG(L"page.dirty", L" dec: inode=%d, page=%d", inode->i_ino, ppage->index);
			inode_dec_dirty_pages(inode);
			f2fs_remove_dirty_inode(inode);
		}

		set_cold_data(ppage);

		err = f2fs_do_write_data_page(&fio);
		if (err) {
			clear_cold_data(ppage);
			if (err == -ENOMEM) 
			{
#if 0
				congestion_wait(BLK_RW_ASYNC, DEFAULT_IO_TIMEOUT);
#endif
				goto retry;
			}
			if (is_dirty)
				set_page_dirty(ppage);
		}
	}
out:
	f2fs_put_page(ppage, 1);
	return err;
}

/*
 * This function tries to get parent node of victim data block, and identifies data block validity. 
   If the block is valid, copy that with cold status and modify parent node.
 * If the parent node is not valid or the data block address is different, the victim data block is ignored. */
static int gc_data_segment(f2fs_sb_info *sbi, f2fs_summary *sum,
		gc_inode_list *gc_list, unsigned int segno, int gc_type, bool force_migrate)
{
	super_block *sb = static_cast<super_block*>(sbi);
	f2fs_summary *entry;
	block_t start_addr;
	int off;
	int phase = 0;
	int submitted = 0;
	unsigned int usable_blks_in_seg = f2fs_usable_blks_in_seg(sbi, segno);

	start_addr = START_BLOCK(sbi, segno);
	
next_step:
	entry = sum;

	for (off = 0; off < usable_blks_in_seg; off++, entry++) 
	{
		struct page *data_page;
		f2fs_inode_info *inode;
		f2fs_inode_info* fi;
		struct node_info dni; /* dnode info for the data */
		unsigned int ofs_in_node, nofs;
		block_t start_bidx;
		nid_t nid = le32_to_cpu(entry->nid);

		/* stop BG_GC if there is not enough free sections.  Or, stop GC if the segment becomes fully valid caused by race condition along with SSR block allocation.	 */
		if ((gc_type == BG_GC && sbi->has_not_enough_free_secs( 0, 0)) ||
			(!force_migrate && get_valid_blocks(sbi, segno, true) ==
							BLKS_PER_SEC(sbi)))
			return submitted;

		if (check_valid_map(sbi, segno, off) == 0)
			continue;

		if (phase == 0) {
			sbi->f2fs_ra_meta_pages( NAT_BLOCK_OFFSET(nid), 1,
							META_NAT, true);
			continue;
		}

		if (phase == 1) {
			f2fs_ra_node_page(sbi, nid);
			continue;
		}

		/* Get an inode by ino with checking validity */
		if (!is_alive(sbi, entry, &dni, start_addr + off, &nofs))
			continue;

		if (phase == 2) {
			f2fs_ra_node_page(sbi, dni.ino);
			continue;
		}

		ofs_in_node = le16_to_cpu(entry->_u._s.ofs_in_node);

		if (phase == 3) 
		{
			inode = sbi->f2fs_iget(dni.ino);
			//fi = F2FS_I(inode);
			if (IS_ERR(inode) || inode->is_bad_inode())			continue;

			if (!down_write_trylock(&inode->i_gc_rwsem[WRITE])) 
			{
				iput(inode);
				sbi->skipped_gc_rwsem++;
				continue;
			}

			start_bidx = f2fs_start_bidx_of_node(nofs, inode) + ofs_in_node;

			if (f2fs_post_read_required(inode)) 
			{
				int err = inode->ra_data_block(start_bidx);

				up_write(&inode->i_gc_rwsem[WRITE]);
				if (err) {
					iput(inode);
					continue;
				}
				add_gc_inode(gc_list, inode);
				continue;
			}

			data_page = inode->f2fs_get_read_data_page(start_bidx, REQ_RAHEAD, true);
			up_write(&inode->i_gc_rwsem[WRITE]);
			if (IS_ERR(data_page)) {
				iput(inode);
				continue;
			}

			f2fs_put_page(data_page, 0);
			add_gc_inode(gc_list, inode);
			continue;
		}

		/* phase 4 */
		inode = F2FS_I(find_gc_inode(gc_list, dni.ino));
		if (inode) 
		{
//			f2fs_inode_info *fi = F2FS_I(inode);
			bool locked = false;
			int err;

			if (S_ISREG(inode->i_mode)) {
				if (!down_write_trylock(&inode->i_gc_rwsem[READ])) continue;
				if (!down_write_trylock(&inode->i_gc_rwsem[WRITE])) 
				{
					sbi->skipped_gc_rwsem++;
					up_write(&inode->i_gc_rwsem[READ]);
					continue;
				}
				locked = true;

				/* wait for all inflight aio data */
				inode_dio_wait(inode);
			}

			start_bidx = f2fs_start_bidx_of_node(nofs, inode) + ofs_in_node;
			if (f2fs_post_read_required(inode))	err = inode->move_data_block(start_bidx, gc_type, segno, off);
			else				err = move_data_page(inode, start_bidx, gc_type, segno, off);

			if (!err && (gc_type == FG_GC || f2fs_post_read_required(inode)))		submitted++;

			if (locked) {
				up_write(&inode->i_gc_rwsem[WRITE]);
				up_write(&inode->i_gc_rwsem[READ]);
			}

			stat_inc_data_blk_count(sbi, 1, gc_type);
		}
	}

	if (++phase < 5)
		goto next_step;

	return submitted;
}

static int __get_victim(f2fs_sb_info *sbi, unsigned int *victim, int gc_type)
{
	struct sit_info *sit_i = sbi->SIT_I();
	int ret;

	down_write(&sit_i->sentry_lock);
	ret = DIRTY_I(sbi)->v_ops->get_victim(sbi, victim, gc_type, NO_CHECK_TYPE, LFS, 0);
	up_write(&sit_i->sentry_lock);
	return ret;
}

static int do_garbage_collect(f2fs_sb_info *sbi, unsigned int start_segno,
				gc_inode_list *gc_list, int gc_type, bool force_migrate)
{
	page *sum_page;
	f2fs_summary_block *sum;
	blk_plug plug;
	unsigned int segno = start_segno;
	unsigned int end_segno = start_segno + sbi->segs_per_sec;
	int seg_freed = 0, migrated = 0;
	unsigned char type = IS_DATASEG(sbi->get_seg_entry( segno)->type) ?	SUM_TYPE_DATA : SUM_TYPE_NODE;
	int submitted = 0;

	if (sbi->__is_large_section())
		end_segno = round_down(end_segno, sbi->segs_per_sec);

	/* zone-capacity can be less than zone-size in zoned devices,
	 * resulting in less than expected usable segments in the zone,
	 * calculate the end segno in the zone which can be garbage collected */
	if (f2fs_sb_has_blkzoned(sbi))
		end_segno -= sbi->segs_per_sec - f2fs_usable_segs_in_sec(sbi, segno);

	sanity_check_seg_type(sbi, sbi->get_seg_entry( segno)->type);

	/* readahead multi ssa blocks those have contiguous address */
	if (sbi->__is_large_section())
		sbi->f2fs_ra_meta_pages( GET_SUM_BLOCK(sbi, segno), end_segno - segno, META_SSA, true);

	/* reference all summary page */
	while (segno < end_segno) 
	{
		sum_page = f2fs_get_sum_page(sbi, segno++);
		if (IS_ERR(sum_page)) {
			int err = PTR_ERR(sum_page);

			end_segno = segno - 1;
			for (segno = start_segno; segno < end_segno; segno++) {
				sum_page = find_get_page(META_MAPPING(sbi),
						GET_SUM_BLOCK(sbi, segno));
				f2fs_put_page(sum_page, 0);
				f2fs_put_page(sum_page, 0);
			}
			return err;
		}
		unlock_page(sum_page);
	}

	blk_start_plug(&plug);

	for (segno = start_segno; segno < end_segno; segno++) 
	{

		/* find segment summary of victim */
		sum_page = find_get_page(META_MAPPING(sbi), GET_SUM_BLOCK(sbi, segno));
		f2fs_put_page(sum_page, 0);

		if (get_valid_blocks(sbi, segno, false) == 0)
			goto freed;
		if (gc_type == BG_GC && sbi->__is_large_section() &&
				migrated >= sbi->migration_granularity)
			goto skip;
		if (!PageUptodate(sum_page) || unlikely(sbi->f2fs_cp_error()))
			goto skip;

		sum = page_address<f2fs_summary_block>(sum_page);
		if (type != GET_SUM_TYPE((&sum->footer))) 
		{
			LOG_ERROR(L"[err] Inconsistent segment (%u) type [%d, %d] in SSA and SIT",
				 segno, type, GET_SUM_TYPE((&sum->footer)));
			sbi->set_sbi_flag(SBI_NEED_FSCK);
			f2fs_stop_checkpoint(sbi, false);
			goto skip;
		}

		/* this is to avoid deadlock:
		 *	- lock_page(sum_page)			- f2fs_replace_block
		 *  - check_valid_map()				- down_write(sentry_lock)
		 *  - down_read(sentry_lock)		- change_curseg()
		 *                                  - lock_page(sum_page)
		 */
		if (type == SUM_TYPE_NODE) submitted += gc_node_segment(sbi, sum->entries, segno, gc_type);
		else 					submitted += gc_data_segment(sbi, sum->entries, gc_list, segno, gc_type,force_migrate);

		stat_inc_seg_count(sbi, type, gc_type);
		migrated++;

freed:
		if (gc_type == FG_GC &&
				get_valid_blocks(sbi, segno, false) == 0)
			seg_freed++;

		if (sbi->__is_large_section() && segno + 1 < end_segno)
			sbi->next_victim_seg[gc_type] = segno + 1;
skip:
		f2fs_put_page(sum_page, 0);
	}

	if (submitted)	f2fs_submit_merged_write(sbi, (type == SUM_TYPE_NODE) ? NODE : DATA);

	blk_finish_plug(&plug);

	stat_inc_call_count(sbi->stat_info);

	return seg_freed;
}


int f2fs_gc(f2fs_sb_info *sbi, bool sync, bool background, bool force, unsigned int segno)
{
	int gc_type = sync ? FG_GC : BG_GC;
	int sec_freed = 0, seg_freed = 0, total_freed = 0;
	int ret = 0;
	struct cp_control cpc;
	unsigned int init_segno = segno;
	gc_inode_list gc_list;
	gc_list.ilist = LIST_HEAD_INIT(gc_list.ilist);
//	gc_list.iroot = RADIX_TREE_INIT(gc_list.iroot, GFP_NOFS);

	unsigned long long last_skipped = sbi->skipped_atomic_files[FG_GC];
	unsigned long long first_skipped;
	unsigned int skipped_round = 0, round = 0;

	//trace_f2fs_gc_begin(sbi->sb, sync, background,
	//			sbi->get_pages( F2FS_DIRTY_NODES),
	//			sbi->get_pages( F2FS_DIRTY_DENTS),
	//			sbi->get_pages( F2FS_DIRTY_IMETA),
	//			free_sections(sbi),
	//			sbi->free_segments(),
	//			reserved_segments(sbi),
	//			sbi->prefree_segments());

	cpc.reason = sbi->__get_cp_reason();
	sbi->skipped_gc_rwsem = 0;
	first_skipped = last_skipped;
gc_more:
	if (unlikely(!(sbi->s_flags & SB_ACTIVE))) 
	{
		ret = -EINVAL;
		goto stop;
	}
	if (unlikely(sbi->f2fs_cp_error())) 
	{
		ret = -EIO;
		goto stop;
	}

	if (gc_type == BG_GC && sbi->has_not_enough_free_secs( 0, 0)) 
	{	/* For example, if there are many prefree_segments below given threshold, we can make them free by checkpoint.
		   Then, we secure free segments which doesn't need fggc any more.		 */
		if (sbi->prefree_segments() && !sbi->is_sbi_flag_set( SBI_CP_DISABLED)) 
		{
			ret = sbi->f2fs_write_checkpoint( &cpc);
			if (ret)		goto stop;
		}
		if (sbi->has_not_enough_free_secs( 0, 0))	gc_type = FG_GC;
	}

	/* f2fs_balance_fs doesn't need to do BG_GC in critical path. */
	if (gc_type == BG_GC && !background) 
	{
		ret = -EINVAL;
		goto stop;
	}
	ret = __get_victim(sbi, &segno, gc_type);
	if (ret)	goto stop;

	seg_freed = do_garbage_collect(sbi, segno, &gc_list, gc_type, force);
	if (gc_type == FG_GC &&	seg_freed == f2fs_usable_segs_in_sec(sbi, segno))		sec_freed++;
	total_freed += seg_freed;

	if (gc_type == FG_GC)
	{
		if (sbi->skipped_atomic_files[FG_GC] > last_skipped || sbi->skipped_gc_rwsem)	skipped_round++;
		last_skipped = sbi->skipped_atomic_files[FG_GC];
		round++;
	}

	if (gc_type == FG_GC && seg_freed)		sbi->cur_victim_sec = NULL_SEGNO;

	if (sync)		goto stop;

	if (sbi->has_not_enough_free_secs( sec_freed, 0)) {
		if (skipped_round <= MAX_SKIP_GC_COUNT ||
					skipped_round * 2 < round) {
			segno = NULL_SEGNO;
			goto gc_more;
		}

		if (first_skipped < last_skipped && (last_skipped - first_skipped) > sbi->skipped_gc_rwsem) 
		{
			f2fs_drop_inmem_pages_all(sbi, true);
			segno = NULL_SEGNO;
			goto gc_more;
		}
		if (gc_type == FG_GC && !sbi->is_sbi_flag_set( SBI_CP_DISABLED))
			ret = sbi->f2fs_write_checkpoint( &cpc);
	}
stop:
	sbi->SIT_I()->last_victim[ALLOC_NEXT] = 0;
	sbi->SIT_I()->last_victim[FLUSH_DEVICE] = init_segno;

	//trace_f2fs_gc_end(sbi->sb, ret, total_freed, sec_freed,
	//			sbi->get_pages( F2FS_DIRTY_NODES),
	//			sbi->get_pages( F2FS_DIRTY_DENTS),
	//			sbi->get_pages( F2FS_DIRTY_IMETA),
	//			free_sections(sbi),
	//			sbi->free_segments(),
	//			reserved_segments(sbi),
	//			sbi->prefree_segments());

	up_write(&sbi->gc_lock);

	put_gc_inode(&gc_list);

	if (sync && !ret)		ret = sec_freed ? 0 : -EAGAIN;
	return ret;
}

#if 0

int __init f2fs_create_garbage_collection_cache(void)
{
	victim_entry_slab = f2fs_kmem_cache_create("f2fs_victim_entry",
					sizeof(struct victim_entry));
	if (!victim_entry_slab)
		return -ENOMEM;
	return 0;
}

void f2fs_destroy_garbage_collection_cache(void)
{
	kmem_cache_destroy(victim_entry_slab);
}
#endif

static void init_atgc_management(struct f2fs_sb_info *sbi)
{
	struct atgc_management *am = &sbi->am;

	if (test_opt(sbi, ATGC) && sbi->SIT_I()->elapsed_time >= DEF_GC_THREAD_AGE_THRESHOLD)
		am->atgc_enabled = true;

	am->root.rb_root.rb_node = NULL;
	am->root.rb_leftmost = NULL;
	INIT_LIST_HEAD(&am->victim_list);
	am->victim_count = 0;

	am->candidate_ratio = DEF_GC_THREAD_CANDIDATE_RATIO;
	am->max_candidate_count = DEF_GC_THREAD_MAX_CANDIDATE_COUNT;
	am->age_weight = DEF_GC_THREAD_AGE_WEIGHT;
	am->age_threshold = DEF_GC_THREAD_AGE_THRESHOLD;
}


void f2fs_build_gc_manager(struct f2fs_sb_info *sbi)
{
	DIRTY_I(sbi)->v_ops = &default_v_ops;

	sbi->gc_pin_file_threshold = DEF_GC_FAILED_PINNED_FILES;

	/* give warm/cold data area from slower device */
	if (sbi->f2fs_is_multi_device() && ! sbi->__is_large_section())
		sbi->SIT_I()->last_victim[ALLOC_NEXT] = GET_SEGNO(sbi, FDEV(0).end_blk) + 1;

	init_atgc_management(sbi);
}

#if 0

static int free_segment_range(struct f2fs_sb_info *sbi,
				unsigned int secs, bool gc_only)
{
	unsigned int segno, next_inuse, start, end;
	struct cp_control cpc = { CP_RESIZE, 0, 0, 0 };
	int gc_mode, gc_type;
	int err = 0;
	int type;

	/* Force block allocation for GC */
	MAIN_SECS(sbi) -= secs;
	start = sbi->MAIN_SECS() * sbi->segs_per_sec;
	end = sbi->MAIN_SEGS() - 1;

	mutex_lock(&DIRTY_I(sbi)->seglist_lock);
	for (gc_mode = 0; gc_mode < MAX_GC_POLICY; gc_mode++)
		if (sbi->SIT_I()->last_victim[gc_mode] >= start)
			sbi->SIT_I()->last_victim[gc_mode] = 0;

	for (gc_type = BG_GC; gc_type <= FG_GC; gc_type++)
		if (sbi->next_victim_seg[gc_type] >= start)
			sbi->next_victim_seg[gc_type] = NULL_SEGNO;
	mutex_unlock(&DIRTY_I(sbi)->seglist_lock);

	/* Move out cursegs from the target range */
	for (type = CURSEG_HOT_DATA; type < NR_CURSEG_PERSIST_TYPE; type++)
		f2fs_allocate_segment_for_resize(sbi, type, start, end);

	/* do GC to move out valid blocks in the range */
	for (segno = start; segno <= end; segno += sbi->segs_per_sec) {
		struct gc_inode_list gc_list = {
			.ilist = LIST_HEAD_INIT(gc_list.ilist),
			.iroot = RADIX_TREE_INIT(gc_list.iroot, GFP_NOFS),
		};

		do_garbage_collect(sbi, segno, &gc_list, FG_GC, true);
		put_gc_inode(&gc_list);

		if (!gc_only && get_valid_blocks(sbi, segno, true)) {
			err = -EAGAIN;
			goto out;
		}
		if (fatal_signal_pending(current)) {
			err = -ERESTARTSYS;
			goto out;
		}
	}
	if (gc_only)
		goto out;

	err = sbi->f2fs_write_checkpoint( &cpc);
	if (err)
		goto out;

	next_inuse = find_next_inuse(sbi->FREE_I(), end + 1, start);
	if (next_inuse <= end) {
		f2fs_err(sbi, "segno %u should be free but still inuse!",
			 next_inuse);
		f2fs_bug_on(sbi, 1);
	}
out:
	sbi->MAIN_SECS() += secs;
	return err;
}

static void update_sb_metadata(struct f2fs_sb_info *sbi, int secs)
{
	struct f2fs_super_block *raw_sb = sbi->F2FS_RAW_SUPER();
	int section_count;
	int segment_count;
	int segment_count_main;
	long long block_count;
	int segs = secs * sbi->segs_per_sec;

	down_write(&sbi->sb_lock);

	section_count = le32_to_cpu(raw_sb->section_count);
	segment_count = le32_to_cpu(raw_sb->segment_count);
	segment_count_main = le32_to_cpu(raw_sb->segment_count_main);
	block_count = le64_to_cpu(raw_sb->block_count);

	raw_sb->section_count = cpu_to_le32(section_count + secs);
	raw_sb->segment_count = cpu_to_le32(segment_count + segs);
	raw_sb->segment_count_main = cpu_to_le32(segment_count_main + segs);
	raw_sb->block_count = cpu_to_le64(block_count +
					(long long)segs * sbi->blocks_per_seg);
	if (sbi->f2fs_is_multi_device()) {
		int last_dev = sbi->s_ndevs - 1;
		int dev_segs =
			le32_to_cpu(raw_sb->devs[last_dev].total_segments);

		raw_sb->devs[last_dev].total_segments =
						cpu_to_le32(dev_segs + segs);
	}

	up_write(&sbi->sb_lock);
}

static void update_fs_metadata(struct f2fs_sb_info *sbi, int secs)
{
	int segs = secs * sbi->segs_per_sec;
	long long blks = (long long)segs * sbi->blocks_per_seg;
	long long user_block_count =
				le64_to_cpu(sbi->F2FS_CKPT()->user_block_count);

	sbi->SM_I()->segment_count = (int)sbi->SM_I()->segment_count + segs;
	sbi->MAIN_SEGS() = (int)sbi->MAIN_SEGS() + segs;
	sbi->MAIN_SECS() += secs;
	sbi->FREE_I()->free_sections = (int)sbi->FREE_I()->free_sections + secs;
	sbi->FREE_I()->free_segments = (int)sbi->FREE_I()->free_segments + segs;
	sbi->F2FS_CKPT()->user_block_count = cpu_to_le64(user_block_count + blks);

	if (sbi->f2fs_is_multi_device()) {
		int last_dev = sbi->s_ndevs - 1;

		FDEV(last_dev).total_segments =
				(int)FDEV(last_dev).total_segments + segs;
		FDEV(last_dev).end_blk =
				(long long)FDEV(last_dev).end_blk + blks;
#ifdef CONFIG_BLK_DEV_ZONED
		FDEV(last_dev).nr_blkz = (int)FDEV(last_dev).nr_blkz +
					(int)(blks >> sbi->log_blocks_per_blkz);
#endif
	}
}

int f2fs_resize_fs(struct f2fs_sb_info *sbi, __u64 block_count)
{
	__u64 old_block_count, shrunk_blocks;
	struct cp_control cpc = { CP_RESIZE, 0, 0, 0 };
	unsigned int secs;
	int err = 0;
	__u32 rem;

	old_block_count = le64_to_cpu(sbi->F2FS_RAW_SUPER()->block_count);
	if (block_count > old_block_count)
		return -EINVAL;

	if (sbi->f2fs_is_multi_device()) {
		int last_dev = sbi->s_ndevs - 1;
		__u64 last_segs = FDEV(last_dev).total_segments;

		if (block_count + last_segs * sbi->blocks_per_seg <=
								old_block_count)
			return -EINVAL;
	}

	/* new fs size should align to section size */
	div_u64_rem(block_count, BLKS_PER_SEC(sbi), &rem);
	if (rem)
		return -EINVAL;

	if (block_count == old_block_count)
		return 0;

	if (sbi->is_sbi_flag_set(SBI_NEED_FSCK)) {
		f2fs_err(sbi, "Should run fsck to repair first.");
		return -EFSCORRUPTED;
	}

	if (test_opt(sbi, DISABLE_CHECKPOINT)) {
		f2fs_err(sbi, "Checkpoint should be enabled.");
		return -EINVAL;
	}

	shrunk_blocks = old_block_count - block_count;
	secs = div_u64(shrunk_blocks, BLKS_PER_SEC(sbi));

	/* stop other GC */
	if (!down_write_trylock(&sbi->gc_lock))
		return -EAGAIN;

	/* stop CP to protect MAIN_SEC in free_segment_range */
	f2fs_lock_op(sbi);
	//auto_lock<rw_semaphore_lock> lock_op(sbi->cp_rwsem);

	spin_lock(&sbi->stat_lock);
	if (shrunk_blocks + sbi->valid_user_blocks() +
		sbi->current_reserved_blocks + sbi->unusable_block_count +
		F2FS_OPTION(sbi).root_reserved_blocks > sbi->user_block_count)
		err = -ENOSPC;
	spin_unlock(&sbi->stat_lock);

	if (err)
		goto out_unlock;

	err = free_segment_range(sbi, secs, true);

out_unlock:
	f2fs_unlock_op(sbi);
	up_write(&sbi->gc_lock);
	if (err)
		return err;

	sbi->set_sbi_flag(SBI_IS_RESIZEFS);

	freeze_super(sbi->sb);
	down_write(&sbi->gc_lock);
	down_write(&sbi->cp_global_sem);

	spin_lock(&sbi->stat_lock);
	if (shrunk_blocks + sbi->valid_user_blocks() +
		sbi->current_reserved_blocks + sbi->unusable_block_count +
		F2FS_OPTION(sbi).root_reserved_blocks > sbi->user_block_count)
		err = -ENOSPC;
	else
		sbi->user_block_count -= shrunk_blocks;
	spin_unlock(&sbi->stat_lock);
	if (err)
		goto out_err;

	err = free_segment_range(sbi, secs, false);
	if (err)
		goto recover_out;

	update_sb_metadata(sbi, -secs);

	err = f2fs_commit_super(sbi, false);
	if (err) {
		update_sb_metadata(sbi, secs);
		goto recover_out;
	}

	update_fs_metadata(sbi, -secs);
	clear_sbi_flag(sbi, SBI_IS_RESIZEFS);
	sbi->set_sbi_flag(SBI_IS_DIRTY);

	err = sbi->f2fs_write_checkpoint( &cpc);
	if (err) {
		update_fs_metadata(sbi, secs);
		update_sb_metadata(sbi, secs);
		f2fs_commit_super(sbi, false);
	}
recover_out:
	if (err) {
		sbi->set_sbi_flag(SBI_NEED_FSCK);
		f2fs_err(sbi, "resize_fs failed, should run fsck to repair!");

		spin_lock(&sbi->stat_lock);
		sbi->user_block_count += shrunk_blocks;
		spin_unlock(&sbi->stat_lock);
	}
out_err:
	up_write(&sbi->cp_global_sem);
	up_write(&sbi->gc_lock);
	thaw_super(sbi->sb);
	clear_sbi_flag(sbi, SBI_IS_RESIZEFS);
	return err;
}

#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/backing-dev.h
 *
 * low-level device information and state which is propagated up through
 * to high-level code.
 */

#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H

#include "linux_comm.h"


//#include "kernel.h"
#include "fs.h"
//#include "sched.h"
#include "blkdev.h"
//#include "device.h"
#include "writeback.h"
//#include "blk-cgroup.h"
#include "backing-dev-defs.h"
//#include "slab.h"

#define SZ_128K				0x00020000
#define VM_READAHEAD_PAGES	(SZ_128K / PAGE_SIZE)

#if 0 //TODO

static inline struct backing_dev_info *bdi_get(struct backing_dev_info *bdi)
{
	kref_get(&bdi->refcnt);
	return bdi;
}

struct backing_dev_info *bdi_get_by_id(u64 id);
void bdi_put(struct backing_dev_info *bdi);

__printf(2, 3)
int bdi_register(struct backing_dev_info *bdi, const char *fmt, ...);
__printf(2, 0)
int bdi_register_va(struct backing_dev_info *bdi, const char *fmt,
		    va_list args);
void bdi_set_owner(struct backing_dev_info *bdi, struct device *owner);
void bdi_unregister(struct backing_dev_info *bdi);

struct backing_dev_info *bdi_alloc(int node_id);

void wb_start_background_writeback(struct bdi_writeback *wb);
void wb_workfn(struct work_struct *work);
void wb_wakeup_delayed(struct bdi_writeback *wb);

void wb_wait_for_completion(struct wb_completion *done);

extern spinlock_t bdi_lock;
extern struct list_head bdi_list;

extern struct workqueue_struct *bdi_wq;
extern struct workqueue_struct *bdi_async_bio_wq;

static inline bool bdi_has_dirty_io(struct backing_dev_info *bdi)
{
	/* @bdi->tot_write_bandwidth is guaranteed to be > 0 if there are
	 * any dirty wbs.  See wb_update_write_bandwidth(). */
	return atomic_long_read(&bdi->tot_write_bandwidth);
}
#endif //TODO

static inline bool wb_has_dirty_io(struct bdi_writeback *wb)
{
	return test_bit(WB_has_dirty_io, wb->state);
}


static inline void __add_wb_stat(struct bdi_writeback *wb, enum wb_stat_item item, s64 amount)
{
	//percpu_counter_add_batch(&wb->stat[item], amount, WB_STAT_BATCH);
	//<YUAN> 简化的percup版本中，不需要用到batch参数
	percpu_counter_add_batch(&wb->stat[item], amount, 0);
}

static inline void inc_wb_stat(struct bdi_writeback *wb, enum wb_stat_item item)
{
	__add_wb_stat(wb, item, 1);
}
static inline void dec_wb_stat(struct bdi_writeback *wb, enum wb_stat_item item)
{
	__add_wb_stat(wb, item, -1);
}
#if 0 //TODO

static inline s64 wb_stat(struct bdi_writeback *wb, enum wb_stat_item item)
{
	return percpu_counter_read_positive(&wb->stat[item]);
}

static inline s64 wb_stat_sum(struct bdi_writeback *wb, enum wb_stat_item item)
{
	return percpu_counter_sum_positive(&wb->stat[item]);
}

extern void wb_writeout_inc(struct bdi_writeback *wb);
#endif //TODO

/*
 * maximal error of a stat counter.
 */
static inline unsigned long wb_stat_error(void)
{
#ifdef CONFIG_SMP
	return nr_cpu_ids * WB_STAT_BATCH;
#else
	return 1;
#endif
}

int bdi_set_min_ratio(struct backing_dev_info *bdi, unsigned int min_ratio);
int bdi_set_max_ratio(struct backing_dev_info *bdi, unsigned int max_ratio);



/*
 * Flags in backing_dev_info::capability
 *
 * BDI_CAP_WRITEBACK:		Supports dirty page writeback, and dirty pages
 *				should contribute to accounting
 * BDI_CAP_WRITEBACK_ACCT:	Automatically account writeback pages
 * BDI_CAP_STRICTLIMIT:		Keep number of dirty pages below bdi threshold
 */
#define BDI_CAP_WRITEBACK		(1 << 0)
#define BDI_CAP_WRITEBACK_ACCT		(1 << 1)
#define BDI_CAP_STRICTLIMIT		(1 << 2)

//extern struct backing_dev_info noop_backing_dev_info;

/**
 * writeback_in_progress - determine whether there is writeback in progress
 * @wb: bdi_writeback of interest
 *
 * Determine whether there is writeback waiting to be handled against a bdi_writeback. */
static inline bool writeback_in_progress(bdi_writeback *wb)
{
	return test_bit(WB_writeback_running, wb->state);
}


static inline backing_dev_info *inode_to_bdi(inode *iinode)
{
	super_block *sb;

//	if (!inode)	return &noop_backing_dev_info;
	if (!iinode) return NULL;

	sb = iinode->i_sb;
#ifdef CONFIG_BLOCK
	//<YUAN> 只处理block device的情况sb_is_blkdev_sb()始终是true
	//if (sb_is_blkdev_sb(sb)) return I_BDEV(inode)->bd_bdi;
#endif
	return sb->s_bdi;
}


static inline int wb_congested(struct bdi_writeback *wb, int cong_bits)
{
	return wb->congested & cong_bits;
}
#if 0 //TODO

long congestion_wait(int sync, long timeout);
long wait_iff_congested(int sync, long timeout);
#endif //TODO

static inline bool mapping_can_writeback(address_space *mapping)
{
	return inode_to_bdi(mapping->host)->capabilities & BDI_CAP_WRITEBACK;
}

static inline int bdi_sched_wait(void *word)
{
//	schedule();
	return 0;
}

#ifdef CONFIG_CGROUP_WRITEBACK

struct bdi_writeback *wb_get_lookup(struct backing_dev_info *bdi,
				    struct cgroup_subsys_state *memcg_css);
struct bdi_writeback *wb_get_create(struct backing_dev_info *bdi,
				    struct cgroup_subsys_state *memcg_css,
				    gfp_t gfp);
void wb_memcg_offline(struct mem_cgroup *memcg);
void wb_blkcg_offline(struct blkcg *blkcg);
int inode_congested(struct inode *inode, int cong_bits);

/**
 * inode_cgwb_enabled - test whether cgroup writeback is enabled on an inode
 * @inode: inode of interest
 *
 * Cgroup writeback requires support from the filesystem.  Also, both memcg and
 * iocg have to be on the default hierarchy.  Test whether all conditions are
 * met.
 *
 * Note that the test result may change dynamically on the same inode
 * depending on how memcg and iocg are configured.
 */
static inline bool inode_cgwb_enabled(struct inode *inode)
{
	struct backing_dev_info *bdi = inode_to_bdi(inode);

	return cgroup_subsys_on_dfl(memory_cgrp_subsys) &&
		cgroup_subsys_on_dfl(io_cgrp_subsys) &&
		(bdi->capabilities & BDI_CAP_WRITEBACK) &&
		(inode->i_sb->s_iflags & SB_I_CGROUPWB);
}

/**
 * wb_find_current - find wb for %current on a bdi
 * @bdi: bdi of interest
 *
 * Find the wb of @bdi which matches both the memcg and blkcg of %current.
 * Must be called under rcu_read_lock() which protects the returend wb.
 * NULL if not found.
 */
static inline struct bdi_writeback *wb_find_current(struct backing_dev_info *bdi)
{
	struct cgroup_subsys_state *memcg_css;
	struct bdi_writeback *wb;

	memcg_css = task_css(current, memory_cgrp_id);
	if (!memcg_css->parent)
		return &bdi->wb;

	wb = radix_tree_lookup(&bdi->cgwb_tree, memcg_css->id);

	/*
	 * %current's blkcg equals the effective blkcg of its memcg.  No
	 * need to use the relatively expensive cgroup_get_e_css().
	 */
	if (likely(wb && wb->blkcg_css == task_css(current, io_cgrp_id)))
		return wb;
	return NULL;
}

/**
 * wb_get_create_current - get or create wb for %current on a bdi
 * @bdi: bdi of interest
 * @gfp: allocation mask
 *
 * Equivalent to wb_get_create() on %current's memcg.  This function is
 * called from a relatively hot path and optimizes the common cases using
 * wb_find_current().
 */
static inline struct bdi_writeback *
wb_get_create_current(struct backing_dev_info *bdi, gfp_t gfp)
{
	struct bdi_writeback *wb;

	rcu_read_lock();
	wb = wb_find_current(bdi);
	if (wb && unlikely(!wb_tryget(wb)))
		wb = NULL;
	rcu_read_unlock();

	if (unlikely(!wb)) {
		struct cgroup_subsys_state *memcg_css;

		memcg_css = task_get_css(current, memory_cgrp_id);
		wb = wb_get_create(bdi, memcg_css, gfp);
		css_put(memcg_css);
	}
	return wb;
}

/**
 * inode_to_wb_is_valid - test whether an inode has a wb associated
 * @inode: inode of interest
 *
 * Returns %true if @inode has a wb associated.  May be called without any
 * locking.
 */
static inline bool inode_to_wb_is_valid(struct inode *inode)
{
	return inode->i_wb;
}

/**
 * inode_to_wb - determine the wb of an inode
 * @inode: inode of interest
 *
 * Returns the wb @inode is currently associated with.  The caller must be
 * holding either @inode->i_lock, the i_pages lock, or the
 * associated wb's list_lock.
 */
static inline struct bdi_writeback *inode_to_wb(const struct inode *inode)
{
#ifdef CONFIG_LOCKDEP
	WARN_ON_ONCE(debug_locks &&
		     (!lockdep_is_held(&inode->i_lock) &&
		      !lockdep_is_held(&inode->i_mapping->i_pages.xa_lock) &&
		      !lockdep_is_held(&inode->i_wb->list_lock)));
#endif
	return inode->i_wb;
}

/**
 * unlocked_inode_to_wb_begin - begin unlocked inode wb access transaction
 * @inode: target inode
 * @cookie: output param, to be passed to the end function
 *
 * The caller wants to access the wb associated with @inode but isn't
 * holding inode->i_lock, the i_pages lock or wb->list_lock.  This
 * function determines the wb associated with @inode and ensures that the
 * association doesn't change until the transaction is finished with
 * unlocked_inode_to_wb_end().
 *
 * The caller must call unlocked_inode_to_wb_end() with *@cookie afterwards and
 * can't sleep during the transaction.  IRQs may or may not be disabled on
 * return.
 */
static inline struct bdi_writeback *
unlocked_inode_to_wb_begin(struct inode *inode, struct wb_lock_cookie *cookie)
{
	rcu_read_lock();

	/*
	 * Paired with store_release in inode_switch_wbs_work_fn() and
	 * ensures that we see the new wb if we see cleared I_WB_SWITCH.
	 */
	cookie->locked = smp_load_acquire(&inode->i_state) & I_WB_SWITCH;

	if (unlikely(cookie->locked))
		xa_lock_irqsave(&inode->i_mapping->i_pages, cookie->flags);

	/*
	 * Protected by either !I_WB_SWITCH + rcu_read_lock() or the i_pages
	 * lock.  inode_to_wb() will bark.  Deref directly.
	 */
	return inode->i_wb;
}

/**
 * unlocked_inode_to_wb_end - end inode wb access transaction
 * @inode: target inode
 * @cookie: @cookie from unlocked_inode_to_wb_begin()
 */
static inline void unlocked_inode_to_wb_end(struct inode *inode,
					    struct wb_lock_cookie *cookie)
{
	if (unlikely(cookie->locked))
		xa_unlock_irqrestore(&inode->i_mapping->i_pages, cookie->flags);

	rcu_read_unlock();
}

#else	/* CONFIG_CGROUP_WRITEBACK */


static inline bool inode_cgwb_enabled(struct inode *inode)
{
	return false;
}

static inline bdi_writeback *wb_find_current(backing_dev_info *bdi)
{
	return &bdi->wb;
}

static inline bdi_writeback *wb_get_create_current(backing_dev_info *bdi, gfp_t gfp)
{
	return &bdi->wb;
}

static inline bool inode_to_wb_is_valid(struct inode *inode)
{
	return true;
}
static inline struct bdi_writeback *inode_to_wb(struct inode *inode)
{
	return &inode_to_bdi(inode)->wb;
}
static inline struct bdi_writeback *
unlocked_inode_to_wb_begin(struct inode *inode, struct wb_lock_cookie *cookie)
{
	return inode_to_wb(inode);
}

static inline void unlocked_inode_to_wb_end(struct inode *inode,  struct wb_lock_cookie *cookie)
{
}
#if 0 //TODO

static inline void wb_memcg_offline(struct mem_cgroup *memcg)
{
}

static inline void wb_blkcg_offline(struct blkcg *blkcg)
{
}
#endif //TOOD

static inline int inode_congested(struct inode *inode, int cong_bits)
{
	return wb_congested(&inode_to_bdi(inode)->wb, cong_bits);
}


#endif	/* CONFIG_CGROUP_WRITEBACK */


static inline int inode_read_congested(struct inode *inode)
{
	return inode_congested(inode, 1 << WB_sync_congested);
}
#if 0 //TODO

static inline int inode_write_congested(struct inode *inode)
{
	return inode_congested(inode, 1 << WB_async_congested);
}

static inline int inode_rw_congested(struct inode *inode)
{
	return inode_congested(inode, (1 << WB_sync_congested) |
				      (1 << WB_async_congested));
}

static inline int bdi_congested(struct backing_dev_info *bdi, int cong_bits)
{
	return wb_congested(&bdi->wb, cong_bits);
}

static inline int bdi_read_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, 1 << WB_sync_congested);
}

static inline int bdi_write_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, 1 << WB_async_congested);
}

static inline int bdi_rw_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, (1 << WB_sync_congested) |
				  (1 << WB_async_congested));
}

const char *bdi_dev_name(struct backing_dev_info *bdi);

#endif //TODO

#if 0

#define NODE_DATA(nid)		(nid)

static inline pg_data_t* page_pgdat(const struct page* page)
{
	return NODE_DATA(page_to_nid(page));
}

static inline void mod_node_state(pglist_data* pgdat, enum node_stat_item item, int delta, int overstep_mode)
{
	struct per_cpu_nodestat __percpu* pcp = pgdat->per_cpu_nodestats;
	s8 __percpu* p = pcp->vm_node_stat_diff + item;
	long o, n, t, z;

	if (vmstat_item_in_bytes(item)) {
		/*
		 * Only cgroups use subpage accounting right now; at
		 * the global level, these items still change in
		 * multiples of whole pages. Store them as pages
		 * internally to keep the per-cpu counters compact.
		 */
		VM_WARN_ON_ONCE(delta & (PAGE_SIZE - 1));
		delta >>= PAGE_SHIFT;
	}

	do {
		z = 0;  /* overflow to node counters */

		/*
		 * The fetching of the stat_threshold is racy. We may apply
		 * a counter threshold to the wrong the cpu if we get
		 * rescheduled while executing here. However, the next
		 * counter update will apply the threshold again and
		 * therefore bring the counter under the threshold again.
		 *
		 * Most of the time the thresholds are the same anyways
		 * for all cpus in a node.
		 */
		t = this_cpu_read(pcp->stat_threshold);

		o = this_cpu_read(*p);
		n = delta + o;

		if (abs(n) > t) {
			int os = overstep_mode * (t >> 1);

			/* Overflow must be added to node counters */
			z = n + os;
			n = -os;
		}
	} while (this_cpu_cmpxchg(*p, o, n) != o);

	if (z)
		node_page_state_add(z, pgdat, item);
}


void dec_node_page_state(page* ppage, enum node_stat_item item)
{
	mod_node_state(page_pgdat(ppage), item, -1, -1);
}

static inline void __dec_node_state(struct pglist_data* pgdat, enum node_stat_item item)
{
	atomic_long_dec(&pgdat->vm_stat[item]);
	atomic_long_dec(&vm_node_stat[item]);
}



static inline void __dec_node_page_state(struct page* page, enum node_stat_item item)
{
	__dec_node_state(page_pgdat(page), item);
}


void dec_node_page_state(struct page* page, enum node_stat_item item)
{
	unsigned long flags;

	//local_irq_save(flags);
	__dec_node_page_state(page, item);
	//local_irq_restore(flags);
}



#endif




#endif	/* _LINUX_BACKING_DEV_H */

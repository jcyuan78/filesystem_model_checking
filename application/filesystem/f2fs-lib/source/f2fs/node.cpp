///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"
// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/node.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux-fs-wrapper.h>
//#include <linux/fs.h>
//#include <linux/f2fs_fs.h>
#include "../../include/f2fs_fs.h"
//#include <linux/mpage.h>
//#include <linux/backing-dev.h>
//#include <linux/blkdev.h>
//#include <linux/pagevec.h>
//#include <linux/swap.h>
//
#include "../../include/f2fs.h"
#include "node.h"
#include "segment.h"
#include "xattr.h"

#include "../mapping.h"
#include "../../include/f2fs-filesystem.h"
//#include <trace/events/f2fs.h>
LOCAL_LOGGER_ENABLE(L"f2fs.node", LOGGER_LEVEL_DEBUGINFO);

//#define on_f2fs_build_free_nids(nmi) mutex_is_locked(&(nm_i)->build_lock)

static struct kmem_cache *nat_entry_slab;
static struct kmem_cache *free_nid_slab;
static struct kmem_cache *nat_entry_set_slab;
static struct kmem_cache *fsync_node_entry_slab;

/* Check whether the given nid is within node id range. */
int f2fs_check_nid_range(struct f2fs_sb_info *sbi, nid_t nid)
{
	if (unlikely(nid < sbi->F2FS_ROOT_INO() || nid >= NM_I(sbi)->max_nid)) 
	{
		sbi->set_sbi_flag(SBI_NEED_FSCK);
//		f2fs_warn(sbi, L"%s: out-of-range nid=%x, run fsck to fix.",  __func__, nid);
		LOG_ERROR(L"[err] out-of-range nid=%x, run fsck to fix.", nid);
		return -EFSCORRUPTED;
	}
	return 0;
}

bool f2fs_available_free_memory(struct f2fs_sb_info *sbi, int type)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	//struct sysinfo val;
	//unsigned long avail_ram;
	unsigned long mem_size = 0;
	bool res = false;

	if (!nm_i)		return true;

	//si_meminfo(&val);
	///* only uses low memory */
	//avail_ram = val.totalram - val.totalhigh;

	MEMORYSTATUSEX mem_state;
	GlobalMemoryStatusEx(&mem_state);
	size_t avail_ram = (mem_state.ullAvailVirtual >> PAGE_SHIFT);
	size_t total_ram = (mem_state.ullAvailVirtual >> PAGE_SHIFT);

	/* give 25%, 25%, 50%, 50%, 50% memory for each components respectively */
	if (type == FREE_NIDS) 
	{
		mem_size = (nm_i->nid_cnt[FREE_NID] * sizeof(struct free_nid)) >> PAGE_SHIFT;
		res = mem_size < ((avail_ram * nm_i->ram_thresh / 100) >> 2);
	}
	else if (type == NAT_ENTRIES)
	{
		mem_size = (nm_i->nat_cnt[TOTAL_NAT] * sizeof(struct nat_entry)) >> PAGE_SHIFT;
		res = mem_size < ((avail_ram * nm_i->ram_thresh / 100) >> 2);
		if (excess_cached_nats(sbi))	res = false;
	} 
	else if (type == DIRTY_DENTS) 
	{
#if 0
		if (sbi->s_bdi->wb.dirty_exceeded)		return false;
#endif
		mem_size = boost::numeric_cast<UINT>( sbi->get_pages( F2FS_DIRTY_DENTS));
		res = mem_size < ((avail_ram * nm_i->ram_thresh / 100) >> 1);
	} else if (type == INO_ENTRIES) {
		int i;

		for (i = 0; i < MAX_INO_ENTRY; i++)
			mem_size += sbi->im[i].ino_num *
						sizeof(struct ino_entry);
		mem_size >>= PAGE_SHIFT;
		res = mem_size < ((avail_ram * nm_i->ram_thresh / 100) >> 1);
	} else if (type == EXTENT_CACHE) {
		mem_size = (atomic_read(&sbi->total_ext_tree) *
				sizeof(struct extent_tree) +
				atomic_read(&sbi->total_ext_node) *
				sizeof(struct extent_node)) >> PAGE_SHIFT;
		res = mem_size < ((avail_ram * nm_i->ram_thresh / 100) >> 1);
	} else if (type == INMEM_PAGES) {
		/* it allows 20% / total_ram for inmemory pages */
		mem_size = boost::numeric_cast<UINT>(sbi->get_pages( F2FS_INMEM_PAGES));
//		res = mem_size < (val.totalram / 5);
		res = mem_size < (total_ram / 5);
	}
	else if (type == DISCARD_CACHE) 
	{
		discard_cmd_control *dcc = sbi->SM_I()->dcc_info;
		//mem_size = (atomic_read(&dcc->discard_cmd_cnt) *	sizeof(struct discard_cmd)) >> PAGE_SHIFT;
		mem_size = (dcc->atomic_read_cmd_count() * sizeof(discard_cmd)) >> PAGE_SHIFT;
		res = mem_size < (avail_ram * nm_i->ram_thresh / 100);
	} 
	else 
	{
#if 0
		if (!sbi->s_bdi->wb.dirty_exceeded)	return true;
#else
		JCASSERT(0);
#endif
	}
	return res;
}

static void clear_node_page_dirty(struct page *page)
{
	if (PageDirty(page)) 
	{
		f2fs_clear_page_cache_dirty_tag(page);
		clear_page_dirty_for_io(page);
		F2FS_P_SB(page)->dec_page_count( F2FS_DIRTY_NODES);
	}
	ClearPageUptodate(page);
}


static page *get_current_nat_page(f2fs_sb_info *sbi, nid_t nid)
{
	return f2fs_get_meta_page_retry(sbi, current_nat_addr(sbi, nid));
}

static struct page *get_next_nat_page(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct page *src_page;
	struct page *dst_page;
	pgoff_t dst_off;
	void *src_addr;
	void *dst_addr;
	struct f2fs_nm_info *nm_i = NM_I(sbi);

	dst_off = next_nat_addr(sbi, current_nat_addr(sbi, nid));

	/* get current nat block page with lock */
	src_page = get_current_nat_page(sbi, nid);
	if (IS_ERR(src_page))
		return src_page;
	dst_page = f2fs_grab_meta_page(sbi, dst_off);
	f2fs_bug_on(sbi, PageDirty(src_page));

	src_addr = page_address<page>(src_page);
	dst_addr = page_address<page>(dst_page);
	memcpy(dst_addr, src_addr, PAGE_SIZE);
	set_page_dirty(dst_page);
	f2fs_put_page(src_page, 1);

	set_to_next_nat(nm_i, nid);

	return dst_page;
}

static nat_entry *__alloc_nat_entry(nid_t nid, bool no_fail)
{
	nat_entry *new_entry;
	if (no_fail)	new_entry = new nat_entry;	//f2fs_kmem_cache_alloc(nat_entry_slab, GFP_F2FS_ZERO);
	else			new_entry = new nat_entry;		// kmem_cache_alloc(nat_entry_slab, GFP_F2FS_ZERO);
	if (new_entry) 
	{
		nat_set_nid(new_entry, nid);
		nat_reset_flag(new_entry);
	}
	return new_entry;
}

static void __free_nat_entry(nat_entry *e)
{
//	kmem_cache_free(nat_entry_slab, e);
	delete e;
}

/* must be locked by nat_tree_lock */
nat_entry *f2fs_nm_info::__init_nat_entry(nat_entry *ne, f2fs_nat_entry *raw_ne, bool no_fail)
{
	LOG_DEBUG(L"[nat] add nat to cache, ino=%d, block_addr=0x%X", ne->ni.ino, ne->ni.blk_addr);
	if (no_fail)	f2fs_radix_tree_insert(&nat_root, nat_get_nid(ne), ne);
	else			if (radix_tree_insert(&nat_root, nat_get_nid(ne), ne))		return NULL;

	if (raw_ne)		node_info_from_raw_nat(&ne->ni, raw_ne);

	spin_lock(&nat_list_lock);
	list_add_tail(&ne->list, &nat_entries);
	spin_unlock(&nat_list_lock);

	nat_cnt[TOTAL_NAT]++;
	nat_cnt[RECLAIMABLE_NAT]++;
	return ne;
}


// 
nat_entry *f2fs_nm_info::__lookup_nat_cache(nid_t n)
{
	nat_entry *ne;
	ne = radix_tree_lookup<nat_entry>(&nat_root, n);

	/* for recent accessed nat entry, move it to tail of lru list */
	if (ne && !get_nat_flag(ne, IS_DIRTY)) 
	{
		spin_lock(&nat_list_lock);
		if (!list_empty(&ne->list))		list_move_tail(&ne->list, &nat_entries);
		spin_unlock(&nat_list_lock);
	}
	return ne;
}

static unsigned int __gang_lookup_nat_cache(f2fs_nm_info *nm_i, nid_t start, unsigned int nr, nat_entry **ep)
{
	return radix_tree_gang_lookup(&nm_i->nat_root, (void **)ep, start, nr);
}

static void __del_from_nat_cache(f2fs_nm_info *nm_i, nat_entry *e)
{
	LOG_DEBUG(L"[nat] delete nat from cache, ino=%d, block_addr=0x%X", e->ni.ino, e->ni.blk_addr);
	radix_tree_delete<nat_entry>(&nm_i->nat_root, nat_get_nid(e));
	nm_i->nat_cnt[TOTAL_NAT]--;
	nm_i->nat_cnt[RECLAIMABLE_NAT]--;
	__free_nat_entry(e);
}

static nat_entry_set *__grab_nat_entry_set(f2fs_nm_info *nm_i, nat_entry *ne)
{
	nid_t set = NAT_BLOCK_OFFSET(ne->ni.nid);
	nat_entry_set *head;

	head = radix_tree_lookup<nat_entry_set>(&nm_i->nat_set_root, set);
	if (!head) {
		head = f2fs_kmem_cache_alloc<nat_entry_set>(nat_entry_set_slab, GFP_NOFS);

		INIT_LIST_HEAD(&head->entry_list);
		INIT_LIST_HEAD(&head->set_list);
		head->set = set;
		head->entry_cnt = 0;
		f2fs_radix_tree_insert(&nm_i->nat_set_root, set, head);
	}
	return head;
}
static void __set_nat_cache_dirty(f2fs_nm_info *nm_i, nat_entry *ne)
{
	nat_entry_set *head=NULL;
	bool new_ne = nat_get_blkaddr(ne) == NEW_ADDR;

	if (!new_ne)		head = __grab_nat_entry_set(nm_i, ne);

	/* update entry_cnt in below condition:
	 * 1. update NEW_ADDR to valid block address;
	 * 2. update old block address to new one;	 */
	if (!new_ne && (get_nat_flag(ne, IS_PREALLOC) || !get_nat_flag(ne, IS_DIRTY)))
		head->entry_cnt++;

	set_nat_flag(ne, IS_PREALLOC, new_ne);

	if (get_nat_flag(ne, IS_DIRTY))
		goto refresh_list;

	nm_i->nat_cnt[DIRTY_NAT]++;
	nm_i->nat_cnt[RECLAIMABLE_NAT]--;
	set_nat_flag(ne, IS_DIRTY, true);
refresh_list:
	spin_lock(&nm_i->nat_list_lock);
	if (new_ne)
		list_del_init(&ne->list);
	else
		list_move_tail(&ne->list, &head->entry_list);
	spin_unlock(&nm_i->nat_list_lock);
}

static void __clear_nat_cache_dirty(struct f2fs_nm_info *nm_i,
		struct nat_entry_set *set, struct nat_entry *ne)
{
	spin_lock(&nm_i->nat_list_lock);
	list_move_tail(&ne->list, &nm_i->nat_entries);
	spin_unlock(&nm_i->nat_list_lock);

	set_nat_flag(ne, IS_DIRTY, false);
	set->entry_cnt--;
	nm_i->nat_cnt[DIRTY_NAT]--;
	nm_i->nat_cnt[RECLAIMABLE_NAT]++;
}

static unsigned int __gang_lookup_nat_set(f2fs_nm_info *nm_i, nid_t start, unsigned int nr, nat_entry_set **ep)
{
	return radix_tree_gang_lookup(&nm_i->nat_set_root, ep, start, nr);
}

bool f2fs_sb_info::f2fs_in_warm_node_list(page *pp)
{
	return NODE_MAPPING(this) == pp->mapping && IS_DNODE(pp) && is_cold_node(pp);
}

void f2fs_init_fsync_node_info(f2fs_sb_info *sbi)
{
	spin_lock_init(&sbi->fsync_node_lock);
	INIT_LIST_HEAD(&sbi->fsync_node_list);
	sbi->fsync_seg_id = 0;
	sbi->fsync_node_num = 0;
}

static unsigned int f2fs_add_fsync_node_entry(f2fs_sb_info *sbi, struct page *page)
{
	fsync_node_entry *fn;
//	unsigned long flags;
	unsigned int seq_id;

	fn = f2fs_kmem_cache_alloc<fsync_node_entry>(fsync_node_entry_slab, GFP_NOFS);

	page->get_page();
	fn->page = page;
	INIT_LIST_HEAD(&fn->list);

	spin_lock_irqsave(&sbi->fsync_node_lock, flags);
	list_add_tail(&fn->list, &sbi->fsync_node_list);
	fn->seq_id = sbi->fsync_seg_id++;
	seq_id = fn->seq_id;
	sbi->fsync_node_num++;
	spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);

	return seq_id;
}
void f2fs_del_fsync_node_entry(f2fs_sb_info *sbi, struct page *page)
{
	fsync_node_entry *fn;
//	unsigned long flags;

	spin_lock_irqsave(&sbi->fsync_node_lock, flags);
	list_for_each_entry(fsync_node_entry, fn, &sbi->fsync_node_list, list) 
	{
		if (fn->page == page) 
		{
			list_del(&fn->list);
			sbi->fsync_node_num--;
			spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);
			kmem_cache_free(fsync_node_entry_slab, fn);
			page->put_page();
			return;
		}
	}
	spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);
	f2fs_bug_on(sbi, 1);
}


void f2fs_reset_fsync_node_info(struct f2fs_sb_info *sbi)
{
//	unsigned long flags;

	spin_lock_irqsave(&sbi->fsync_node_lock, flags);
	sbi->fsync_seg_id = 0;
	spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);
}


int f2fs_need_dentry_mark(f2fs_sb_info *sbi, nid_t nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct nat_entry *e;
	bool need = false;

	down_read(&nm_i->nat_tree_lock);
	e = nm_i->__lookup_nat_cache( nid);
	if (e) {
		if (!get_nat_flag(e, IS_CHECKPOINTED) &&
				!get_nat_flag(e, HAS_FSYNCED_INODE))
			need = true;
	}
	up_read(&nm_i->nat_tree_lock);
	return need;
}

bool f2fs_is_checkpointed_node(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct nat_entry *e;
	bool is_cp = true;

	down_read(&nm_i->nat_tree_lock);
	e = nm_i->__lookup_nat_cache( nid);
	if (e && !get_nat_flag(e, IS_CHECKPOINTED))
		is_cp = false;
	up_read(&nm_i->nat_tree_lock);
	return is_cp;
}

bool f2fs_need_inode_block_update(f2fs_sb_info *sbi, nid_t ino)
{
	f2fs_nm_info *nm_i = NM_I(sbi);
	nat_entry *e;
	bool need_update = true;

	down_read(&nm_i->nat_tree_lock);
	e = nm_i->__lookup_nat_cache(ino);
	if (e && get_nat_flag(e, HAS_LAST_FSYNC) &&
		(get_nat_flag(e, IS_CHECKPOINTED) || get_nat_flag(e, HAS_FSYNCED_INODE)))
		need_update = false;
	up_read(&nm_i->nat_tree_lock);
	return need_update;
}


/* must be locked by nat_tree_lock */
void f2fs_nm_info::cache_nat_entry(nid_t nid, struct f2fs_nat_entry *ne)
{
//	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct nat_entry *new_entry, *e;

	new_entry = __alloc_nat_entry(nid, false);
	if (!new_entry)		return;

	down_write(&nat_tree_lock);
	e = __lookup_nat_cache(nid);
	if (!e)		e = __init_nat_entry(new_entry, ne, false);
	else
	{
		f2fs_bug_on(m_sbi, nat_get_ino(e) != le32_to_cpu(ne->ino)
			|| nat_get_blkaddr(e) != le32_to_cpu(ne->block_addr)
			|| nat_get_version(e) != ne->version);
	}
	up_write(&nat_tree_lock);
	if (e != new_entry)		__free_nat_entry(new_entry);
}


static void set_node_addr(struct f2fs_sb_info *sbi, struct node_info *ni,
			block_t new_blkaddr, bool fsync_done)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct nat_entry *e;
	struct nat_entry *new_entry_buf = __alloc_nat_entry(ni->nid, true);

	down_write(&nm_i->nat_tree_lock);
	e = nm_i->__lookup_nat_cache(ni->nid);
	if (!e) {
		e = nm_i->__init_nat_entry( new_entry_buf, NULL, true);
		copy_node_info(&e->ni, ni);
		f2fs_bug_on(sbi, ni->blk_addr == NEW_ADDR);
	} else if (new_blkaddr == NEW_ADDR) {
		/*
		 * when nid is reallocated,
		 * previous nat entry can be remained in nat cache.
		 * So, reinitialize it with new_entry_buf information.
		 */
		copy_node_info(&e->ni, ni);
		f2fs_bug_on(sbi, ni->blk_addr != NULL_ADDR);
	}
	/* let's free early to reduce memory consumption */
	if (e != new_entry_buf)
		__free_nat_entry(new_entry_buf);

	/* sanity check */
	f2fs_bug_on(sbi, nat_get_blkaddr(e) != ni->blk_addr);
	f2fs_bug_on(sbi, nat_get_blkaddr(e) == NULL_ADDR &&
			new_blkaddr == NULL_ADDR);
	f2fs_bug_on(sbi, nat_get_blkaddr(e) == NEW_ADDR &&
			new_blkaddr == NEW_ADDR);
	f2fs_bug_on(sbi, __is_valid_data_blkaddr(nat_get_blkaddr(e)) &&
			new_blkaddr == NEW_ADDR);

	/* increment version no as node is removed */
	if (nat_get_blkaddr(e) != NEW_ADDR && new_blkaddr == NULL_ADDR) {
		unsigned char version = nat_get_version(e);

		nat_set_version(e, inc_node_version(version));
	}

	/* change address */
	nat_set_blkaddr(e, new_blkaddr);
	if (!__is_valid_data_blkaddr(new_blkaddr))
		set_nat_flag(e, IS_CHECKPOINTED, false);
	__set_nat_cache_dirty(nm_i, e);

	/* update fsync_mark if its inode nat entry is still alive */
	if (ni->nid != ni->ino)
		e = nm_i->__lookup_nat_cache(ni->ino);
	if (e) {
		if (fsync_done && ni->nid == ni->ino)
			set_nat_flag(e, HAS_FSYNCED_INODE, true);
		set_nat_flag(e, HAS_LAST_FSYNC, fsync_done);
	}
	up_write(&nm_i->nat_tree_lock);
}

int f2fs_try_to_free_nats(f2fs_sb_info *sbi, int nr_shrink)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	int nr = nr_shrink;

	if (!down_write_trylock(&nm_i->nat_tree_lock))
		return 0;

	spin_lock(&nm_i->nat_list_lock);
	while (nr_shrink) {
		struct nat_entry *ne;

		if (list_empty(&nm_i->nat_entries))
			break;

		ne = list_first_entry(&nm_i->nat_entries,
					struct nat_entry, list);
		list_del(&ne->list);
		spin_unlock(&nm_i->nat_list_lock);

		__del_from_nat_cache(nm_i, ne);
		nr_shrink--;

		spin_lock(&nm_i->nat_list_lock);
	}
	spin_unlock(&nm_i->nat_list_lock);

	up_write(&nm_i->nat_tree_lock);
	return nr - nr_shrink;
}

//<YUAN> 获取inode的block地址，放入ni.blk_addr
int f2fs_nm_info::f2fs_get_node_info(nid_t nid, /*out*/ node_info* ni)
{
//	f2fs_nm_info *nm_i = NM_I(sbi);
	curseg_info *curseg = m_sbi->CURSEG_I(CURSEG_HOT_DATA);
	f2fs_journal *journal = &curseg->journal;
	nid_t start_nid = START_NID(nid);
	f2fs_nat_block *nat_blk;
	page *page = NULL;
	f2fs_nat_entry ne;
	nat_entry *e;
	pgoff_t index;
	block_t blkaddr;
	int i;

	ni->nid = nid;

	/* Check nat cache */
	down_read(&nat_tree_lock);
	e = __lookup_nat_cache(nid);
	if (e) 
	{
		ni->ino = nat_get_ino(e);
		ni->blk_addr = nat_get_blkaddr(e);
		ni->version = nat_get_version(e);
		up_read(&nat_tree_lock);
		LOG_DEBUG(L"[nat] got node info from cache, ino=%d, block_addr=0x%X", ni->ino, ni->blk_addr);
		return 0;
	}

	memset(&ne, 0, sizeof(struct f2fs_nat_entry));

	/* Check current segment summary */
	down_read(&curseg->journal_rwsem);
	i = f2fs_lookup_journal_in_cursum(journal, NAT_JOURNAL, nid, 0);
	if (i >= 0) 
	{
		ne = nat_in_journal(journal, i);
		node_info_from_raw_nat(ni, &ne);
		LOG_DEBUG(L"[nat] got node info from journal, ino=%d, block_addr=0x%X", ni->ino, ni->blk_addr);
	}
	up_read(&curseg->journal_rwsem);
	if (i >= 0) 
	{
		up_read(&nat_tree_lock);
		goto cache;
	}

	/* Fill node_info from nat page */
	index = current_nat_addr(m_sbi, nid);		//<YUAN> index为block address
	up_read(&nat_tree_lock);

	page = m_sbi->f2fs_get_meta_page(index);
	if (IS_ERR(page))	return (int)PTR_ERR(page);

	nat_blk = /*(struct f2fs_nat_block *)*/page_address<f2fs_nat_block>(page);
	ne = nat_blk->entries[nid - start_nid];
	node_info_from_raw_nat(ni, &ne);
	f2fs_put_page(page, 1);
	LOG_DEBUG(L"[nat] got node info from nat, ino=%d, block_addr=0x%X", ni->ino, ni->blk_addr);

cache:
	blkaddr = le32_to_cpu(ne.block_addr);
	if (__is_valid_data_blkaddr(blkaddr) &&	!m_sbi->f2fs_is_valid_blkaddr(blkaddr, DATA_GENERIC_ENHANCE))
		return -EFAULT;
	/* cache nat entry */
	cache_nat_entry(nid, &ne);
	return 0;
}


/* readahead MAX_RA_NODE number of node pages. */
static void f2fs_ra_node_pages(page *parent, int start, int n)
{
	struct f2fs_sb_info *sbi = F2FS_P_SB(parent);
	blk_plug plug;
	int i, end;
	nid_t nid;

	blk_start_plug(&plug);

	/* Then, try readahead for siblings of the desired node */
	end = start + n;
	end = min(end, NIDS_PER_BLOCK);
	for (i = start; i < end; i++) 
	{
		nid = get_nid(parent, i, false);
		f2fs_ra_node_page(sbi, nid);
	}

	blk_finish_plug(&plug);
}

// 获取pgofs的下一个offset?
pgoff_t f2fs_get_next_page_offset(dnode_of_data *dn, pgoff_t pgofs)
{
	const long direct_index =	ADDRS_PER_INODE(dn->inode);
	const long direct_blks =	ADDRS_PER_BLOCK(dn->inode);
	const long indirect_blks =	ADDRS_PER_BLOCK(dn->inode) * NIDS_PER_BLOCK;
	unsigned int skipped_unit = ADDRS_PER_BLOCK(dn->inode);
	int cur_level = dn->cur_level;
	int max_level = dn->max_level;
	pgoff_t base = 0;

	if (!dn->max_level) 	return pgofs + 1;

	while (max_level-- > cur_level)		skipped_unit *= NIDS_PER_BLOCK;

	switch (dn->max_level) 
	{
	case 3:
		base += 2 * indirect_blks;
//		fallthrough;
	case 2:
		base += 2 * direct_blks;
//		fallthrough;
	case 1:
		base += direct_index;
		break;
	default:
		f2fs_bug_on(F2FS_I_SB(dn->inode), 1);
	}
	return ((pgofs - base) / skipped_unit + 1) * skipped_unit + base;
}

/* The maximum depth is four. Offset[0] will have raw inode offset.*/
// 获取逻辑地址block在map中的路径，返回路径保存在offset中。返回深度。
static int get_node_path(struct inode *inode, long block, int offset[4], unsigned int noffset[4])
{
	const long direct_index = ADDRS_PER_INODE(inode);	// i_addr的数量，92
	const long direct_blks = ADDRS_PER_BLOCK(inode);	// 一个indirect block做包含的block数量
	const long dptrs_per_blk = NIDS_PER_BLOCK;
	const long indirect_blks = ADDRS_PER_BLOCK(inode) * NIDS_PER_BLOCK;
	const long dindirect_blks = indirect_blks * NIDS_PER_BLOCK;
	int n = 0;
	int level = 0;

	noffset[0] = 0;

	if (block < direct_index) 
	{	// direct block，指针在inode的i_addr中
		offset[n] = block;
		goto got;
	}
	block -= direct_index;
	if (block < direct_blks) 
	{	// 在第一个direct block中
		offset[n++] = NODE_DIR1_BLOCK;	//指向第一层的索引，
		noffset[n] = 1;
		offset[n] = block;				//指向第二层的索引
		level = 1;
		goto got;
	}
	block -= direct_blks;
	if (block < direct_blks) 
	{
		offset[n++] = NODE_DIR2_BLOCK;
		noffset[n] = 2;
		offset[n] = block;
		level = 1;
		goto got;
	}
	block -= direct_blks;
	if (block < indirect_blks) 
	{
		offset[n++] = NODE_IND1_BLOCK;
		noffset[n] = 3;
		offset[n++] = block / direct_blks;
		noffset[n] = 4 + offset[n - 1];
		offset[n] = block % direct_blks;
		level = 2;
		goto got;
	}
	block -= indirect_blks;
	if (block < indirect_blks) 
	{
		offset[n++] = NODE_IND2_BLOCK;
		noffset[n] = 4 + dptrs_per_blk;
		offset[n++] = block / direct_blks;
		noffset[n] = 5 + dptrs_per_blk + offset[n - 1];
		offset[n] = block % direct_blks;
		level = 2;
		goto got;
	}
	block -= indirect_blks;
	if (block < dindirect_blks) 
	{
		offset[n++] = NODE_DIND_BLOCK;
		noffset[n] = 5 + (dptrs_per_blk * 2);
		offset[n++] = block / indirect_blks;
		noffset[n] = 6 + (dptrs_per_blk * 2) +  offset[n - 1] * (dptrs_per_blk + 1);
		offset[n++] = (block / direct_blks) % dptrs_per_blk;
		noffset[n] = 7 + (dptrs_per_blk * 2) +  offset[n - 2] * (dptrs_per_blk + 1) +  offset[n - 1];
		offset[n] = block % direct_blks;
		level = 3;
		goto got;
	} 
	else 
	{
		return -E2BIG;
	}
got:
	return level;
}

/* Caller should call f2fs_put_dnode(dn).
 * Also, it should grab and release a rwsem by calling f2fs_lock_op() and f2fs_unlock_op() only if mode is set with
 ALLOC_NODE. */
int f2fs_get_dnode_of_data(dnode_of_data *dn, pgoff_t index, int mode)
{
	// 获取index对应的物理地址，填入dnode_of_data。
	f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	struct page *npage[4];
	struct page *parent = NULL;
	int offset[4];
	unsigned int noffset[4];
	nid_t nids[4];
	int level, i = 0;
	int err = 0;

	// 对于index block，获取逻辑地址的路径，结果在offset中
	level = get_node_path(dn->inode, index, offset, noffset);
	if (level < 0)	return level;

	nids[0] = dn->inode->i_ino;
	npage[0] = dn->inode_page;

	// 读取inode的data
	if (!npage[0]) 
	{
		npage[0] = sbi->f2fs_get_node_page(nids[0]);
		if (IS_ERR(npage[0]))	return PTR_ERR(npage[0]);
	}

	/* if inline_data is set, should not report any block indices */
	if (f2fs_has_inline_data(dn->inode) && index) 
	{
		err = -ENOENT;
		f2fs_put_page(npage[0], 1);
		goto release_out;
	}

	// inode->indirect->direct->data

	parent = npage[0];
	if (level != 0)	nids[1] = get_nid(parent, offset[0], true);		// 获取第一级inderect的nid
	dn->inode_page = npage[0];
	dn->inode_page_locked = true;

	/* get indirect or direct nodes */
	for (i = 1; i <= level; i++) 
	{
		bool done = false;
		if (!nids[i] && mode == ALLOC_NODE) 
		{	/* alloc new node */
			if (!NM_I(sbi)->f2fs_alloc_nid(&(nids[i]))) 
			{
				err = -ENOSPC;
				goto release_pages;
			}

			dn->nid = nids[i];
			npage[i] = dn->inode->f2fs_new_node_page(dn, noffset[i]);
			if (IS_ERR(npage[i])) 
			{
				f2fs_alloc_nid_failed(sbi, nids[i]);
				err = PTR_ERR(npage[i]);
				goto release_pages;
			}

			set_nid(parent, offset[i - 1], nids[i], i == 1);
			sbi->nm_info->f2fs_alloc_nid_done(nids[i]);
			done = true;
		} 
		else if (mode == LOOKUP_NODE_RA && i == level && level > 1) 
		{
			npage[i] = f2fs_get_node_page_ra(parent, offset[i - 1]);
			if (IS_ERR(npage[i])) 
			{
				err = PTR_ERR(npage[i]);
				goto release_pages;
			}
			done = true;
		}
		if (i == 1)
		{// 第0级为inode，不同的释放方法
			dn->inode_page_locked = false;
			unlock_page(parent);
		}
		else	{	f2fs_put_page(parent, 1);	}

		if (!done) 
		{
			npage[i] = sbi->f2fs_get_node_page(nids[i]);	// 获取下一级node的data
			if (IS_ERR(npage[i])) 
			{
				err = PTR_ERR(npage[i]);
				f2fs_put_page(npage[0], 0);
				goto release_out;
			}
		}
		if (i < level) 
		{
			parent = npage[i];
			nids[i + 1] = get_nid(parent, offset[i], false);	//获取下一级的nid
		}
	}
	dn->nid = nids[level];
	dn->ofs_in_node = offset[level];
	dn->node_page = npage[level];
	dn->data_blkaddr = f2fs_data_blkaddr(dn);	// 根据offset, 获取nid，然后获取物理block
	return 0;

release_pages:
	f2fs_put_page(parent, 1);
	if (i > 1)
		f2fs_put_page(npage[0], 0);
release_out:
	dn->inode_page = NULL;
	dn->node_page = NULL;
	if (err == -ENOENT) {
		dn->cur_level = i;
		dn->max_level = level;
		dn->ofs_in_node = offset[level];
	}
	return err;
}
static int truncate_node(struct dnode_of_data *dn)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	struct node_info ni;
	int err;
	pgoff_t index;

	err = NM_I(sbi)->f2fs_get_node_info( dn->nid, &ni);
	if (err)
		return err;

	/* Deallocate node address */
	f2fs_invalidate_blocks(sbi, ni.blk_addr);
	sbi->dec_valid_node_count(dn->inode, dn->nid == dn->inode->i_ino);
	set_node_addr(sbi, &ni, NULL_ADDR, false);

	if (dn->nid == dn->inode->i_ino) {
		f2fs_remove_orphan_inode(sbi, dn->nid);
		dec_valid_inode_count(sbi);
		F2FS_I(dn->inode)->f2fs_inode_synced();
	}

	clear_node_page_dirty(dn->node_page);
	sbi->set_sbi_flag(SBI_IS_DIRTY);

	index = dn->node_page->index;
	f2fs_put_page(dn->node_page, 1);

	invalidate_mapping_pages(NODE_MAPPING(sbi),	index, index);

	dn->node_page = NULL;
//	trace_f2fs_truncate_node(dn->inode, dn->nid, ni.blk_addr);

	return 0;
}

static int truncate_dnode(dnode_of_data *dn)
{
	page *ppage;
	int err;

	if (dn->nid == 0)
		return 1;

	/* get direct node */
	ppage = dn->inode->m_sbi->f2fs_get_node_page(dn->nid);
	if (PTR_ERR(ppage) == -ENOENT)
		return 1;
	else if (IS_ERR(ppage))
		return PTR_ERR(ppage);

	/* Make dnode_of_data for parameter */
	dn->node_page = ppage;
	dn->ofs_in_node = 0;
	f2fs_truncate_data_blocks(dn);
	err = truncate_node(dn);
	if (err)
		return err;

	return 1;
}

static int truncate_nodes(dnode_of_data *dn, unsigned int nofs, 	int ofs, int depth)
{
	dnode_of_data rdn = *dn;
	page *ppage;
	f2fs_node *rn;
	nid_t child_nid;
	unsigned int child_nofs;
	int freed = 0;
	int i, ret;

	if (dn->nid == 0)
		return NIDS_PER_BLOCK + 1;

//	trace_f2fs_truncate_nodes_enter(dn->inode, dn->nid, dn->data_blkaddr);

	ppage = dn->inode->m_sbi->f2fs_get_node_page(dn->nid);
	if (IS_ERR(ppage))
	{
//		trace_f2fs_truncate_nodes_exit(dn->inode, PTR_ERR(ppage));
		return PTR_ERR(ppage);
	}

	f2fs_ra_node_pages(ppage, ofs, NIDS_PER_BLOCK);

	rn = F2FS_NODE(ppage);
	if (depth < 3) {
		for (i = ofs; i < NIDS_PER_BLOCK; i++, freed++) {
			child_nid = le32_to_cpu(rn->in.nid[i]);
			if (child_nid == 0)
				continue;
			rdn.nid = child_nid;
			ret = truncate_dnode(&rdn);
			if (ret < 0)
				goto out_err;
			if (set_nid(ppage, i, 0, false))
				dn->node_changed = true;
		}
	} else {
		child_nofs = nofs + ofs * (NIDS_PER_BLOCK + 1) + 1;
		for (i = ofs; i < NIDS_PER_BLOCK; i++) {
			child_nid = le32_to_cpu(rn->in.nid[i]);
			if (child_nid == 0) {
				child_nofs += NIDS_PER_BLOCK + 1;
				continue;
			}
			rdn.nid = child_nid;
			ret = truncate_nodes(&rdn, child_nofs, 0, depth - 1);
			if (ret == (NIDS_PER_BLOCK + 1)) {
				if (set_nid(ppage, i, 0, false))
					dn->node_changed = true;
				child_nofs += ret;
			} else if (ret < 0 && ret != -ENOENT) {
				goto out_err;
			}
		}
		freed = child_nofs;
	}

	if (!ofs) {
		/* remove current indirect node */
		dn->node_page = ppage;
		ret = truncate_node(dn);
		if (ret)
			goto out_err;
		freed++;
	} else {
		f2fs_put_page(ppage, 1);
	}
//	trace_f2fs_truncate_nodes_exit(dn->inode, freed);
	return freed;

out_err:
	f2fs_put_page(ppage, 1);
//	trace_f2fs_truncate_nodes_exit(dn->inode, ret);
	return ret;
}

static int truncate_partial_nodes(dnode_of_data *dn, f2fs_inode *ri, int *offset, int depth)
{
	page *pages[2];
	nid_t nid[3];
	nid_t child_nid;
	int err = 0;
	int i;
	int idx = depth - 2;

	nid[0] = le32_to_cpu(ri->i_nid[offset[0] - NODE_DIR1_BLOCK]);
	if (!nid[0])
		return 0;

	/* get indirect nodes in the path */
	for (i = 0; i < idx + 1; i++) {
		/* reference count'll be increased */
		pages[i] = dn->inode->m_sbi->f2fs_get_node_page(nid[i]);
		if (IS_ERR(pages[i])) 
		{
			err = PTR_ERR(pages[i]);
			idx = i - 1;
			goto fail;
		}
		nid[i + 1] = get_nid(pages[i], offset[i + 1], false);
	}

	f2fs_ra_node_pages(pages[idx], offset[idx + 1], NIDS_PER_BLOCK);

	/* free direct nodes linked to a partial indirect node */
	for (i = offset[idx + 1]; i < NIDS_PER_BLOCK; i++) {
		child_nid = get_nid(pages[idx], i, false);
		if (!child_nid)
			continue;
		dn->nid = child_nid;
		err = truncate_dnode(dn);
		if (err < 0)
			goto fail;
		if (set_nid(pages[idx], i, 0, false))
			dn->node_changed = true;
	}

	if (offset[idx + 1] == 0) {
		dn->node_page = pages[idx];
		dn->nid = nid[idx];
		err = truncate_node(dn);
		if (err)
			goto fail;
	} else {
		f2fs_put_page(pages[idx], 1);
	}
	offset[idx]++;
	offset[idx + 1] = 0;
	idx--;
fail:
	for (i = idx; i >= 0; i--)
		f2fs_put_page(pages[i], 1);

//	trace_f2fs_truncate_partial_nodes(dn->inode, nid, depth, err);

	return err;
}

/* All the block addresses of data and nodes should be nullified. */
int f2fs_truncate_inode_blocks(inode *iinode, pgoff_t from)
{
	f2fs_sb_info *sbi = F2FS_I_SB(iinode);
	int err = 0, cont = 1;
	int level, offset[4];
	unsigned int noffset[4];
	unsigned int nofs = 0;
	f2fs_inode *ri;
	dnode_of_data dn;
	page *ppage;

//	trace_f2fs_truncate_inode_blocks_enter(iinode, from);

	level = get_node_path(iinode, from, offset, noffset);
	if (level < 0) 
	{
//		trace_f2fs_truncate_inode_blocks_exit(iinode, level);
		return level;
	}

	ppage = sbi->f2fs_get_node_page( iinode->i_ino);
	if (IS_ERR(ppage)) 
	{
//		trace_f2fs_truncate_inode_blocks_exit(iinode, PTR_ERR(ppage));
		return PTR_ERR(ppage);
	}

	dn.set_new_dnode(iinode, ppage, NULL, 0);
	unlock_page(ppage);

	ri = F2FS_INODE(ppage);
	switch (level) 
	{
	case 0:
	case 1:
		nofs = noffset[1];
		break;
	case 2:
		nofs = noffset[1];
		if (!offset[level - 1])			goto skip_partial;
		err = truncate_partial_nodes(&dn, ri, offset, level);
		if (err < 0 && err != -ENOENT)
			goto fail;
		nofs += 1 + NIDS_PER_BLOCK;
		break;
	case 3:
		nofs = 5 + 2 * NIDS_PER_BLOCK;
		if (!offset[level - 1])
			goto skip_partial;
		err = truncate_partial_nodes(&dn, ri, offset, level);
		if (err < 0 && err != -ENOENT)
			goto fail;
		break;
	default:
//		BUG();
		JCASSERT(0);
	}

skip_partial:
	while (cont) 
	{
		dn.nid = le32_to_cpu(ri->i_nid[offset[0] - NODE_DIR1_BLOCK]);
		switch (offset[0]) 
		{
		case NODE_DIR1_BLOCK:
		case NODE_DIR2_BLOCK:
			err = truncate_dnode(&dn);
			break;

		case NODE_IND1_BLOCK:
		case NODE_IND2_BLOCK:
			err = truncate_nodes(&dn, nofs, offset[1], 2);
			break;

		case NODE_DIND_BLOCK:
			err = truncate_nodes(&dn, nofs, offset[1], 3);
			cont = 0;
			break;

		default:
//			BUG();
			JCASSERT(0);
		}
		if (err < 0 && err != -ENOENT)
			goto fail;
		if (offset[1] == 0 && ri->i_nid[offset[0] - NODE_DIR1_BLOCK]) 
		{
			lock_page(ppage);
			BUG_ON(ppage->mapping != NODE_MAPPING(sbi));
			f2fs_wait_on_page_writeback(ppage, NODE, true, true);
			ri->i_nid[offset[0] - NODE_DIR1_BLOCK] = 0;
			set_page_dirty(ppage);
			unlock_page(ppage);
		}
		offset[1] = 0;
		offset[0]++;
		nofs += err;
	}
fail:
	f2fs_put_page(ppage, 0);
//	trace_f2fs_truncate_inode_blocks_exit(iinode, err);
	return err > 0 ? 0 : err;
}

/* caller must lock inode page */
int f2fs_truncate_xattr_node(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	nid_t nid = F2FS_I(inode)->i_xattr_nid;
	struct dnode_of_data dn;
	struct page *npage;
	int err;

	if (!nid)
		return 0;

	npage = sbi->f2fs_get_node_page( nid);
	if (IS_ERR(npage))
		return PTR_ERR(npage);

	dn.set_new_dnode(inode, NULL, npage, nid);
	err = truncate_node(&dn);
	if (err) {
		f2fs_put_page(npage, 1);
		return err;
	}

	f2fs_i_xnid_write(F2FS_I(inode), 0);

	return 0;
}

/* Caller should grab and release a rwsem by calling f2fs_lock_op() and f2fs_unlock_op(). */
int f2fs_remove_inode_page(struct inode *inode)
{
	struct dnode_of_data dn;
	int err;

	dn.set_new_dnode(inode, NULL, NULL, inode->i_ino);
	err = f2fs_get_dnode_of_data(&dn, 0, LOOKUP_NODE);
	if (err)
		return err;

	err = f2fs_truncate_xattr_node(inode);
	if (err) {
		f2fs_put_dnode(&dn);
		return err;
	}

	/* remove potential inline_data blocks */
	if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||	S_ISLNK(inode->i_mode))
		f2fs_truncate_data_blocks_range(&dn, 1);

	/* 0 is possible, after f2fs_new_inode() has failed */
	if (unlikely(F2FS_I_SB(inode)->f2fs_cp_error()))
	{
		f2fs_put_dnode(&dn);
		return -EIO;
	}

	if (unlikely(inode->i_blocks != 0 && inode->i_blocks != 8))
	{
		LOG_WARNING(L"f2fs_remove_inode_page: inconsistent i_blocks, ino:%lu, iblocks:%llu",
			inode->i_ino, (unsigned long long)inode->i_blocks);
		set_sbi_flag(F2FS_I_SB(inode), SBI_NEED_FSCK);
	}

	/* will put inode & node pages */
	err = truncate_node(&dn);
	if (err) 
	{
		f2fs_put_dnode(&dn);
		return err;
	}
	return 0;
}

// 创建一个当前inode对应的ondisk page，存放inode的ondisk data
//page *f2fs_new_inode_page(f2fs_inode_info *inode)

page *f2fs_inode_info::f2fs_new_inode_page()
{
	dnode_of_data dn;
	/* allocate inode page for new inode */
	dn.set_new_dnode(this, NULL, NULL, i_ino);
	/* caller should f2fs_put_page(page, 1); */
	return f2fs_new_node_page(&dn, 0);
}

//page *f2fs_new_node_page(dnode_of_data *dn, unsigned int ofs)
page *f2fs_inode_info::f2fs_new_node_page(dnode_of_data *dn, unsigned int ofs)
{
	JCASSERT(dn->inode == this);
//	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	node_info new_ni;
	page *ppage;
	int err;

	if (unlikely(is_inode_flag_set(FI_NO_ALLOC)))
		return ERR_PTR<page>((INT64)(-EPERM));

	ppage = f2fs_grab_cache_page(NODE_MAPPING(m_sbi), dn->nid, false);
	if (!ppage) return ERR_PTR<page>((INT64)(-ENOMEM));

#ifdef _DEBUG
	jcvos::Utf8ToUnicode(ppage->m_type, "inode");
	ppage->m_description = L"page of " + m_description;
	LOG_DEBUG(L"got page for inode (%s), index=%d, addr=%llX", m_description.c_str(), ppage->index, ppage->virtual_add);
#endif 

	if (unlikely((err = m_sbi->inc_valid_node_count(dn->inode, !ofs))))
		goto fail;

#ifdef CONFIG_F2FS_CHECK_FS
	err = NM_I(m_sbi)->f2fs_get_node_info( dn->nid, &new_ni);
	if (err) {
		dec_valid_node_count(m_sbi, dn->inode, !ofs);
		goto fail;
	}
	f2fs_bug_on(m_sbi, new_ni.blk_addr != NULL_ADDR);
#endif
	new_ni.nid = dn->nid;
	new_ni.ino = i_ino;
	new_ni.blk_addr = NULL_ADDR;
	new_ni.flag = 0;
	new_ni.version = 0;
	set_node_addr(m_sbi, &new_ni, NEW_ADDR, false);

	f2fs_wait_on_page_writeback(ppage, NODE, true, true);
	fill_node_footer(ppage, dn->nid, i_ino, ofs, true);
	set_cold_node(ppage, S_ISDIR(dn->inode->i_mode));
	if (!PageUptodate(ppage))
		SetPageUptodate(ppage);
	if (set_page_dirty(ppage))
		dn->node_changed = true;
	if (f2fs_has_xattr_block(ofs))
		f2fs_i_xnid_write(dn->inode, dn->nid);
	if (ofs == 0)	inc_valid_inode_count(m_sbi);
	return ppage;
fail:
	clear_node_page_dirty(ppage);
	f2fs_put_page(ppage, 1);
	return ERR_PTR<page>(err);
}

/* Caller should do after getting the following values.
 * 0: f2fs_put_page(page, 0)
 * LOCKED_PAGE or error: f2fs_put_page(page, 1) */
int f2fs_sb_info::read_node_page(page *page, int op_flags)
{
	node_info ni;
	f2fs_io_info fio;
	fio.sbi = this;
	fio.type = NODE;
	fio.op = REQ_OP_READ;
	fio.op_flags = op_flags;
	fio.encrypted_page = NULL;
	//<YUAN>
	fio.page = page;
	int err;

	if (PageUptodate(page)) 
	{
		if (!f2fs_inode_chksum_verify(this, page)) 
		{
			ClearPageUptodate(page);
			return -EFSBADCRC;
		}
		return LOCKED_PAGE;
	}

	//<YUAN>获取inode的block地址，放入ni.blk_addr
	err = nm_info->f2fs_get_node_info(page->index, &ni);
	if (err)
	{
		LOG_ERROR(L"[err] failed on getting node info, err=%d", err);
		return err;
	}

	if (unlikely(ni.blk_addr == NULL_ADDR) ||	is_sbi_flag_set( SBI_IS_SHUTDOWN)) 
	{
		LOG_ERROR(L"[err] check address failed, blk_addr=%d, flag=0x%X, err=%d", ni.blk_addr, s_flag, -ENOENT);
		ClearPageUptodate(page);
		return -ENOENT;
	}

	fio.new_blkaddr = fio.old_blkaddr = ni.blk_addr;
#ifdef _DEBUG
	page->m_block_addr = ni.blk_addr;
#endif

	//<YUAN> 实际读取inode, block地址：fio->new_blkaddr, buffer: fio->page
	err = f2fs_submit_page_bio(&fio);
	if (!err)	f2fs_update_iostat(this, FS_NODE_READ_IO, F2FS_BLKSIZE);
	return err;
}

#if 0

/*
 * Readahead a node page
 */
void f2fs_ra_node_page(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct page *apage;
	int err;

	if (!nid)
		return;
	if (f2fs_check_nid_range(sbi, nid))
		return;

	apage = xa_load(&NODE_MAPPING(sbi)->i_pages, nid);
	if (apage)
		return;

	apage = f2fs_grab_cache_page(NODE_MAPPING(sbi), nid, false);
	if (!apage)
		return;

	err = read_node_page(apage, REQ_RAHEAD);
	f2fs_put_page(apage, err ? 1 : 0);
}
#endif

//<YUAN> nid: inode number
page *f2fs_sb_info::__get_node_page(pgoff_t nid, page *parent, int start)
{
	page *ptr_page;
	int err;

	if (!nid)
	{
		LOG_ERROR(L"nid==0, err=%d", (-ENOENT));
		return ERR_PTR<page>(-ENOENT);
	}
	if (f2fs_check_nid_range(this, nid)) 	return ERR_PTR<page>(-EINVAL);
repeat:
	ptr_page = f2fs_grab_cache_page(NODE_MAPPING(this), nid, false);
	if (!ptr_page)		return ERR_PTR<page>(-ENOMEM);
#ifdef _DEBUG
	jcvos::Utf8ToUnicode(ptr_page->m_type, "node");
#endif
	LOG_DEBUG(L"[page_track], page=%p, addr=%p, type=node, index=%d", ptr_page, ptr_page->virtual_add, nid);
	err = read_node_page(ptr_page, 0);
	if (err < 0) 
	{
		LOG_ERROR(L"[err] failed on reading node page, err=%d", err);
		f2fs_put_page(ptr_page, 1);
		return ERR_PTR<page>(err);
	} 
	else if (err == LOCKED_PAGE) 
	{
		err = 0;
		goto page_hit;
	}

	if (parent)		f2fs_ra_node_pages(parent, start + 1, MAX_RA_NODE);

	lock_page(ptr_page);

	if (unlikely(ptr_page->mapping != NODE_MAPPING(this))) 
	{
		f2fs_put_page(ptr_page, 1);
		goto repeat;
	}

	if (unlikely(!PageUptodate(ptr_page))) 
	{
		err = -EIO;
		goto out_err;
	}

	if (!f2fs_inode_chksum_verify(this, ptr_page)) 
	{
		err = -EFSBADCRC;
		goto out_err;
	}
page_hit:
	if (unlikely(nid != nid_of_node(ptr_page))) 
	{
		LOG_ERROR(L"[err] inconsistent node block, nid:%lu, node_footer[nid:%u,ino:%u,ofs:%u,cpver:%llu,blkaddr:%u]",
			  nid, nid_of_node(ptr_page), ino_of_node(ptr_page),
			  ofs_of_node(ptr_page), cpver_of_node(ptr_page),
			  next_blkaddr_of_node(ptr_page));
		err = -EINVAL;
	out_err:
		ClearPageUptodate(ptr_page);
		f2fs_put_page(ptr_page, 1);
		return ERR_PTR<page>(err);
	}
	return ptr_page;
//#undef ERR_PTR
}

//<YUAN> nid：inode number
//struct page *f2fs_get_node_page(struct f2fs_sb_info *sbi, pgoff_t nid)
//{
//	return __get_node_page(sbi, nid, NULL, 0);
//}

#if 0

struct page *f2fs_get_node_page_ra(struct page *parent, int start)
{
	struct f2fs_sb_info *sbi = F2FS_P_SB(parent);
	nid_t nid = get_nid(parent, start, false);

	return __get_node_page(sbi, nid, parent, start);
}

#endif

static void flush_inline_data(f2fs_sb_info* sbi, nid_t ino)
{
//	struct inode* inode;
//	struct page* page;
	/* should flush inline_data before evict_inode */
	inode * node = sbi->ilookup(ino);
	if (!node) 	return;
	f2fs_inode_info* fi = F2FS_I(node);
	JCASSERT(fi);
	fi->flush_inline_data();
}

void f2fs_inode_info::flush_inline_data(void)
{

	int ret;
	page * ppage = f2fs_pagecache_get_page(i_mapping, 0, FGP_LOCK|FGP_NOWAIT, 0);
	if (!ppage)						goto iput_out;
	if (!PageUptodate(ppage))		goto page_out;
	if (!PageDirty(ppage))			goto page_out;
	if (!clear_page_dirty_for_io(ppage))		goto page_out;

	ret = f2fs_write_inline_data(this, ppage);
	inode_dec_dirty_pages(this);
	f2fs_remove_dirty_inode(this);
	if (ret)	set_page_dirty(ppage);
page_out:
	f2fs_put_page(ppage, 1);
iput_out:
	iput(this);
}

static page *last_fsync_dnode(f2fs_sb_info *sbi, nid_t ino)
{
	pgoff_t index;
	struct pagevec pvec;
	page *last_page = NULL;
	int nr_pages;

	pagevec_init(&pvec);
	index = 0;

	while ((nr_pages = pagevec_lookup_tag(&pvec, NODE_MAPPING(sbi), &index,
				PAGECACHE_TAG_DIRTY))) {
		int i;

		for (i = 0; i < nr_pages; i++) {
			page *ppage = pvec.pages[i];

			if (unlikely(sbi->f2fs_cp_error())) {
				f2fs_put_page(last_page, 0);
				pagevec_release(&pvec);
				return ERR_PTR<page>(-EIO);
			}

			if (!IS_DNODE(ppage) || !is_cold_node(ppage))
				continue;
			if (ino_of_node(ppage) != ino)
				continue;

			lock_page(ppage);

			if (unlikely(ppage->mapping != NODE_MAPPING(sbi))) {
continue_unlock:
				unlock_page(ppage);
				continue;
			}
			if (ino_of_node(ppage) != ino)
				goto continue_unlock;

			if (!PageDirty(ppage)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			if (last_page)
				f2fs_put_page(last_page, 0);

			ppage->get_page();
			last_page = ppage;
			unlock_page(ppage);
		}
		pagevec_release(&pvec);
//		cond_resched();
	}
	return last_page;
}


static int __write_node_page(page *ppage, bool atomic, bool *submitted, writeback_control *wbc, bool do_balance,	enum iostat_type io_type, unsigned int *seq_id)
{
	LOG_DEBUG(L"[IO] write node page: index=%d", ppage->index);
	f2fs_sb_info *sbi = F2FS_P_SB(ppage);
	nid_t nid;
	struct node_info ni;
	struct f2fs_io_info fio;

	fio.sbi = sbi;
	fio.ino = ino_of_node(ppage);
	fio.type = NODE;
	fio.op = REQ_OP_WRITE;
	fio.op_flags = wbc_to_write_flags(wbc);
	fio.page = ppage;
	fio.encrypted_page = NULL;
	fio.submitted = false;
	fio.io_type = io_type;
	fio.io_wbc = wbc;

	unsigned int seq;

//	trace_f2fs_writepage(ppage, NODE);

	if (unlikely(sbi->f2fs_cp_error())) {
		if (sbi->is_sbi_flag_set( SBI_IS_CLOSE)) {
			ClearPageUptodate(ppage);
			sbi->dec_page_count(F2FS_DIRTY_NODES);
			unlock_page(ppage);
			return 0;
		}
		goto redirty_out;
	}

	if (unlikely(sbi->is_sbi_flag_set( SBI_POR_DOING)))
		goto redirty_out;

	if (!sbi->is_sbi_flag_set( SBI_CP_DISABLED) &&	wbc->sync_mode == WB_SYNC_NONE && IS_DNODE(ppage) && is_cold_node(ppage))
		goto redirty_out;

	/* get old block addr of this node page */
	nid = nid_of_node(ppage);
	f2fs_bug_on(sbi, ppage->index != nid);

	if (NM_I(sbi)->f2fs_get_node_info( nid, &ni))
		goto redirty_out;

	if (wbc->for_reclaim) 
	{
		if (!down_read_trylock(&sbi->node_write))		goto redirty_out;
	} 
	else 
	{
		down_read(&sbi->node_write);
	}

	/* This page is already truncated */
	if (unlikely(ni.blk_addr == NULL_ADDR)) {
		ClearPageUptodate(ppage);
		sbi->dec_page_count(F2FS_DIRTY_NODES);
		up_read(&sbi->node_write);
		unlock_page(ppage);
		return 0;
	}

	if (__is_valid_data_blkaddr(ni.blk_addr) &&
		!sbi->f2fs_is_valid_blkaddr(ni.blk_addr,
					DATA_GENERIC_ENHANCE)) {
		up_read(&sbi->node_write);
		goto redirty_out;
	}

	if (atomic && !test_opt(sbi, NOBARRIER))
		fio.op_flags |= REQ_PREFLUSH | REQ_FUA;

	/* should add to global list before clearing PAGECACHE status */
	if (sbi->f2fs_in_warm_node_list( ppage)) {
		seq = f2fs_add_fsync_node_entry(sbi, ppage);
		if (seq_id)
			*seq_id = seq;
	}

#if 1	//TODO
	set_page_writeback(ppage);
#endif
	ClearPageError(ppage);

	fio.old_blkaddr = ni.blk_addr;
	f2fs_do_write_node_page(nid, &fio);
	set_node_addr(sbi, &ni, fio.new_blkaddr, is_fsync_dnode(ppage));
	sbi->dec_page_count(F2FS_DIRTY_NODES);
	up_read(&sbi->node_write);

	if (wbc->for_reclaim) {
		f2fs_submit_merged_write_cond(sbi, NULL, ppage, 0, NODE);
		submitted = NULL;
	}

	unlock_page(ppage);

	if (unlikely(sbi->f2fs_cp_error())) {
		f2fs_submit_merged_write(sbi, NODE);
		submitted = NULL;
	}
	if (submitted)
		*submitted = fio.submitted;

	if (do_balance)
		sbi->f2fs_balance_fs(false);
	return 0;

redirty_out:
#if 1 // TODO
	redirty_page_for_writepage(wbc, ppage);
#endif
	return AOP_WRITEPAGE_ACTIVATE;
}

int f2fs_move_node_page(page *node_page, int gc_type)
{
	int err = 0;

	if (gc_type == FG_GC) 
	{
		writeback_control wbc;
		wbc.sync_mode = WB_SYNC_ALL;
		wbc.nr_to_write = 1;
		wbc.for_reclaim = 0;

		f2fs_wait_on_page_writeback(node_page, NODE, true, true);

		set_page_dirty(node_page);

		if (!clear_page_dirty_for_io(node_page)) 
		{
			err = -EAGAIN;
			goto out_page;
		}

		if (__write_node_page(node_page, false, NULL, &wbc, false, FS_GC_NODE_IO, NULL)) 
		{
			err = -EAGAIN;
			unlock_page(node_page);
		}
		goto release_page;
	} 
	else 
	{	/* set page dirty and write it */
		if (!PageWriteback(node_page))		set_page_dirty(node_page);
	}
out_page:
	unlock_page(node_page);
release_page:
	f2fs_put_page(node_page, 0);
	return err;
}

//static int f2fs_write_node_page(struct page *page, struct writeback_control *wbc)
int Cf2fsNodeMapping::write_page(page * page, writeback_control * wbc)
{
#if 0
	return __write_node_page(page, false, NULL, wbc, false, FS_NODE_IO, NULL);
#else
	JCASSERT(0); return -1;
#endif
}

int f2fs_fsync_node_pages(f2fs_sb_info *sbi, struct inode *inode, struct writeback_control *wbc, bool atomic,
			unsigned int *seq_id)
{
	pgoff_t index;
	struct pagevec pvec;
	int ret = 0;
	struct page *last_page = NULL;
	bool marked = false;
	nid_t ino = inode->i_ino;
	int nr_pages;
	int nwritten = 0;

	if (atomic) 
	{
		last_page = last_fsync_dnode(sbi, ino);
		if (IS_ERR_OR_NULL(last_page))
			return PTR_ERR_OR_ZERO(last_page);
	}
retry:
	pagevec_init(&pvec);
	index = 0;

	while ((nr_pages = pagevec_lookup_tag(&pvec, NODE_MAPPING(sbi), &index,
				PAGECACHE_TAG_DIRTY))) {
		int i;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];
			bool submitted = false;

			if (unlikely(sbi->f2fs_cp_error())) {
				f2fs_put_page(last_page, 0);
				pagevec_release(&pvec);
				ret = -EIO;
				goto out;
			}

			if (!IS_DNODE(page) || !is_cold_node(page))
				continue;
			if (ino_of_node(page) != ino)
				continue;

			lock_page(page);

			if (unlikely(page->mapping != NODE_MAPPING(sbi))) {
continue_unlock:
				unlock_page(page);
				continue;
			}
			if (ino_of_node(page) != ino)
				goto continue_unlock;

			if (!PageDirty(page) && page != last_page) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			f2fs_wait_on_page_writeback(page, NODE, true, true);

			set_fsync_mark(page, 0);
			set_dentry_mark(page, 0);

			if (!atomic || page == last_page) 
			{
				set_fsync_mark(page, 1);
				if (IS_INODE(page)) 
				{
					if (is_inode_flag_set(inode, FI_DIRTY_INODE))
						F2FS_I(inode)->f2fs_update_inode(page);
					set_dentry_mark(page, f2fs_need_dentry_mark(sbi, ino));
				}
				/* may be written by other thread */
				if (!PageDirty(page))
					set_page_dirty(page);
			}

			if (!clear_page_dirty_for_io(page))
				goto continue_unlock;

			ret = __write_node_page(page, atomic &&
						page == last_page,
						&submitted, wbc, true,
						FS_NODE_IO, seq_id);
			if (ret) {
				unlock_page(page);
				f2fs_put_page(last_page, 0);
				break;
			} else if (submitted) {
				nwritten++;
			}

			if (page == last_page) {
				f2fs_put_page(page, 0);
				marked = true;
				break;
			}
		}
		pagevec_release(&pvec);
//		cond_resched();

		if (ret || marked)
			break;
	}
	if (!ret && atomic && !marked) {
		f2fs_debug(sbi, L"Retry to write fsync mark: ino=%u, idx=%lx", ino, last_page->index);
		lock_page(last_page);
		f2fs_wait_on_page_writeback(last_page, NODE, true, true);
		set_page_dirty(last_page);
		unlock_page(last_page);
		goto retry;
	}
out:
	if (nwritten)
		f2fs_submit_merged_write_cond(sbi, NULL, NULL, ino, NODE);
	return ret ? -EIO : 0;
}

static int f2fs_match_ino(inode *iinode, unsigned long ino, void *data)
{
	f2fs_sb_info *sbi = F2FS_I_SB(iinode);
	bool clean;

	if (iinode->i_ino != ino)	return 0;

	if (!is_inode_flag_set(iinode, FI_DIRTY_INODE))		return 0;

	spin_lock(&sbi->inode_lock[DIRTY_META]);
//	clean = list_empty(&F2FS_I(iinode)->gdirty_list);
	clean = sbi->list_empty(DIRTY_META);
	spin_unlock(&sbi->inode_lock[DIRTY_META]);

	if (clean)		return 0;

	iinode = igrab(iinode);
	if (!iinode)		return 0;
	return 1;
}


static bool flush_dirty_inode(struct page *page)
{
	struct f2fs_sb_info *sbi = F2FS_P_SB(page);
	struct inode *inode;
	nid_t ino = ino_of_node(page);

	inode = sbi->find_inode_nowait(ino, f2fs_match_ino, NULL);
	if (!inode)	return false;

	F2FS_I(inode)->f2fs_update_inode( page);
	unlock_page(page);

	iput(inode);
	return true;
}

void f2fs_flush_inline_data(f2fs_sb_info *sbi)
{
	pgoff_t index = 0;
	pagevec pvec;
	int nr_pages;

	pagevec_init(&pvec);

	// 查找标记为dirty的page，添加到pagevec中
	while ((nr_pages = pagevec_lookup_tag(&pvec, NODE_MAPPING(sbi), &index, PAGECACHE_TAG_DIRTY))) 
	{
		int i;
		for (i = 0; i < nr_pages; i++) 
		{
			page *ppage = pvec.pages[i];
			if (!IS_DNODE(ppage))			continue;

			auto_lock<page_auto_lock> page_locker(*ppage);
			//lock_page(ppage);

			if (unlikely(ppage->mapping != NODE_MAPPING(sbi))) 
			{
//continue_unlock:
//				unlock_page(ppage);
				continue;
			}

			if (!PageDirty(ppage)) continue;		/* someone wrote it for us */
			//{				
			//	goto continue_unlock;
			//}

			/* flush inline_data, if it's async context. */
			if (is_inline_node(ppage))
			{
				clear_inline_node(ppage);
				//unlock_page(ppage);
				flush_inline_data(sbi, ino_of_node(ppage));
				continue;
			}
			// unlock_page(ppage);
		}
		pagevec_release(&pvec);
//		cond_resched();
	}
}

int f2fs_sync_node_pages(f2fs_sb_info *sbi, writeback_control *wbc, bool do_balance, enum iostat_type io_type)
{
	pgoff_t index;
	struct pagevec pvec;
	int step = 0;
	int nwritten = 0;
	int ret = 0;
	int nr_pages, done = 0;

	pagevec_init(&pvec);

next_step:
	index = 0;
	// 根据mapping地址查找page，然后放入pvec中
	while (!done && (nr_pages = pagevec_lookup_tag(&pvec, NODE_MAPPING(sbi), &index, PAGECACHE_TAG_DIRTY))) 
	{
		int i;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];
			bool submitted = false;
			bool may_dirty = true;

			/* give a priority to WB_SYNC threads */
			if (atomic_read(&sbi->wb_sync_req[NODE]) &&
					wbc->sync_mode == WB_SYNC_NONE) {
				done = 1;
				break;
			}

			/* flushing sequence with step:
			 * 0. indirect nodes
			 * 1. dentry dnodes
			 * 2. file dnodes			 */
			if (step == 0 && IS_DNODE(page)) 		continue;
			if (step == 1 && (!IS_DNODE(page) || is_cold_node(page))) 		continue;
			if (step == 2 && (!IS_DNODE(page) || !is_cold_node(page)))		continue;
lock_node:
			if (wbc->sync_mode == WB_SYNC_ALL)
				lock_page(page);
			else if (!trylock_page(page))
				continue;

			if (unlikely(page->mapping != NODE_MAPPING(sbi))) {
continue_unlock:
				unlock_page(page);
				continue;
			}

			if (!PageDirty(page)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			/* flush inline_data/inode, if it's async context. */
			if (!do_balance)
				goto write_node;

			/* flush inline_data */
			if (is_inline_node(page))
			{
				clear_inline_node(page);
				unlock_page(page);
				flush_inline_data(sbi, ino_of_node(page));
				goto lock_node;
			}

			/* flush dirty inode */
			if (IS_INODE(page) && may_dirty)
			{
				may_dirty = false;
				if (flush_dirty_inode(page))				goto lock_node;
			}
write_node:
			f2fs_wait_on_page_writeback(page, NODE, true, true);

			if (!clear_page_dirty_for_io(page))
				goto continue_unlock;

			set_fsync_mark(page, 0);
			set_dentry_mark(page, 0);

			ret = __write_node_page(page, false, &submitted,
						wbc, do_balance, io_type, NULL);
			if (ret)
				unlock_page(page);
			else if (submitted)
				nwritten++;

			if (--wbc->nr_to_write == 0)
				break;
		}
		pagevec_release(&pvec);
//		cond_resched();

		if (wbc->nr_to_write == 0) {
			step = 2;
			break;
		}
	}

	if (step < 2) {
		if (!sbi->is_sbi_flag_set( SBI_CP_DISABLED) &&
				wbc->sync_mode == WB_SYNC_NONE && step == 1)
			goto out;
		step++;
		goto next_step;
	}
out:
	if (nwritten)
		f2fs_submit_merged_write(sbi, NODE);

	if (unlikely(sbi->f2fs_cp_error()))
		return -EIO;
	return ret;
}

int f2fs_wait_on_node_pages_writeback(f2fs_sb_info *sbi, 	unsigned int seq_id)
{
	struct fsync_node_entry *fn;
	struct page *page;
	struct list_head *head = &sbi->fsync_node_list;
//	unsigned long flags;
	unsigned int cur_seq_id = 0;
	int ret2, ret = 0;

	while (seq_id && cur_seq_id < seq_id) {
		spin_lock_irqsave(&sbi->fsync_node_lock, flags);
		if (list_empty(head)) {
			spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);
			break;
		}
		fn = list_first_entry(head, struct fsync_node_entry, list);
		if (fn->seq_id > seq_id) {
			spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);
			break;
		}
		cur_seq_id = fn->seq_id;
		page = fn->page;
		page->get_page();
		spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);

		f2fs_wait_on_page_writeback(page, NODE, true, false);
		if (TestClearPageError(page))
			ret = -EIO;

		page->put_page();

		if (ret)
			break;
	}

	ret2 = filemap_check_errors(NODE_MAPPING(sbi));
	if (!ret)
		ret = ret2;

	return ret;
}



//static int f2fs_write_node_pages(address_space *mapping, struct writeback_control *wbc)
int Cf2fsNodeMapping::write_pages(writeback_control *wbc)
{
#if 1
//	f2fs_sb_info* sbi = (f2fs_sb_info*)host->i_sb->s_fs_info; //F2FS_M_SB(mapping);
	f2fs_sb_info* sbi = dynamic_cast<f2fs_sb_info*>(host->i_sb);
	JCASSERT(sbi);
	blk_plug plug;
	long diff;

	if (unlikely(sbi->is_sbi_flag_set( SBI_POR_DOING))) 	goto skip_write;

	/* balancing f2fs's metadata in background */
	f2fs_balance_fs_bg(sbi, true);

	/* collect a number of dirty node pages and write together */
	if (wbc->sync_mode != WB_SYNC_ALL && sbi->get_pages( F2FS_DIRTY_NODES) <	nr_pages_to_skip(sbi, NODE))
		goto skip_write;

	if (wbc->sync_mode == WB_SYNC_ALL)				atomic_inc(&sbi->wb_sync_req[NODE]);
	else if (atomic_read(&sbi->wb_sync_req[NODE]))		goto skip_write;

//	trace_f2fs_writepages(mapping->host, wbc, NODE);

	diff = nr_pages_to_write(sbi, NODE, wbc);
	blk_start_plug(&plug);
	f2fs_sync_node_pages(sbi, wbc, true, FS_NODE_IO);
	blk_finish_plug(&plug);
	wbc->nr_to_write = max((long)0, wbc->nr_to_write - diff);

	if (wbc->sync_mode == WB_SYNC_ALL)		atomic_dec(&sbi->wb_sync_req[NODE]);
	return 0;

skip_write:
	wbc->pages_skipped += sbi->get_pages( F2FS_DIRTY_NODES);
//	trace_f2fs_writepages(mapping->host, wbc, NODE);
#else
	JCASSERT(0)
#endif
	return 0;
}


//static int f2fs_set_node_page_dirty(struct page *page)
int Cf2fsNodeMapping::set_node_page_dirty(page* page)
{

//	trace_f2fs_set_page_dirty(page, NODE);
	if (!PageUptodate(page))		SetPageUptodate(page);
#ifdef CONFIG_F2FS_CHECK_FS
	if (IS_INODE(page))
		f2fs_inode_chksum_set(F2FS_P_SB(page), page);
#endif
	if (!PageDirty(page)) 
	{
		__set_page_dirty_nobuffers(page);
		F2FS_P_SB(page)->inc_page_count(F2FS_DIRTY_NODES);
		f2fs_set_page_private(page, 0);
		return 1;
	}
	return 0;
}

/*
 * Structure of the f2fs node operations
 */
//const struct address_space_operations f2fs_node_aops = {
//	.writepage	= f2fs_write_node_page,
//	.writepages	= f2fs_write_node_pages,
//	.set_page_dirty	= f2fs_set_node_page_dirty,
//	.invalidatepage	= f2fs_invalidate_page,
//	.releasepage	= f2fs_release_page,
//#ifdef CONFIG_MIGRATION
//	.migratepage	= f2fs_migrate_page,
//#endif
//};


int f2fs_nm_info::__insert_free_nid(free_nid *i)
{
	//struct f2fs_nm_info *nm_i = NM_I(sbi);
	int err = radix_tree_insert(&free_nid_root, i->nid, i);
	if (err) return err;

	nid_cnt[FREE_NID]++;
	list_add_tail(&i->list, &free_nid_list);
	return 0;
}

void f2fs_nm_info::__remove_free_nid(free_nid *i, enum nid_state state)
{
	//struct f2fs_nm_info *nm_i = NM_I(sbi);
	//f2fs_bug_on(sbi, state != i->state);
	JCASSERT(state == i->state);
	nid_cnt[state]--;
	if (state == FREE_NID)	list_del(&i->list);
	radix_tree_delete<free_nid>(&free_nid_root, i->nid);
}

//static void __move_free_nid(f2fs_sb_info *sbi, free_nid *i, enum nid_state org_state, enum nid_state dst_state)
void f2fs_nm_info::__move_free_nid(free_nid *i, enum nid_state org_state, enum nid_state dst_state)
{
//	f2fs_nm_info *nm_i = NM_I(sbi);
//	f2fs_bug_on(sbi, org_state != i->state);
	JCASSERT(org_state == i->state);
	i->state = dst_state;
	nid_cnt[org_state]--;
	nid_cnt[dst_state]++;

	switch (dst_state) 
	{
	case PREALLOC_NID:
		list_del(&i->list);
		break;
	case FREE_NID:
		list_add_tail(&i->list, &free_nid_list);
		break;
	default:
		BUG_ON(1);
	}
}

//static void update_free_nid_bitmap(f2fs_sb_info *sbi, nid_t nid, bool set, bool build)
void f2fs_nm_info::update_free_nid_bitmap(nid_t nid, bool set, bool build)
{
//	struct f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned int nat_ofs = NAT_BLOCK_OFFSET(nid);
	unsigned int nid_ofs = nid - START_NID(nid);

	if (!test_bit_le(nat_ofs, nat_block_bitmap)) return;

	if (set) 
	{
		if (test_bit_le(nid_ofs, free_nid_bitmap[nat_ofs]))		return;
		__set_bit_le(nid_ofs, free_nid_bitmap[nat_ofs]);
		free_nid_count[nat_ofs]++;
	} 
	else 
	{
		if (!test_bit_le(nid_ofs, free_nid_bitmap[nat_ofs]))		return;
		__clear_bit_le(nid_ofs, free_nid_bitmap[nat_ofs]);
		if (!build)		free_nid_count[nat_ofs]--;
	}
}
/* return if the nid is recognized as free */
//static bool add_free_nid(f2fs_sb_info *sbi, nid_t nid, bool build, bool update)
bool f2fs_nm_info::add_free_nid(nid_t nid, bool build, bool update)
{
//	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i, *e;
	struct nat_entry *ne;
	int err = -EINVAL;
	bool ret = false;

	/* 0 nid should not be used */
	if (unlikely(nid == 0))		return false;
	if (unlikely(f2fs_check_nid_range(m_sbi, nid)))		return false;

	i = f2fs_kmem_cache_alloc<free_nid>(free_nid_slab, GFP_NOFS);
	i->nid = nid;
	i->state = FREE_NID;

	radix_tree_preload(GFP_NOFS | __GFP_NOFAIL);

	{ auto_lock<spin_locker> lock(nid_list_lock);
	//	spin_lock(&nid_list_lock);

	if (build)
	{
		/*
		 *   Thread A             Thread B
		 *  - f2fs_create
		 *   - f2fs_new_inode
		 *    - f2fs_alloc_nid
		 *     - __insert_nid_to_list(PREALLOC_NID)
		 *                     - f2fs_balance_fs_bg
		 *                      - f2fs_build_free_nids
		 *                       - __f2fs_build_free_nids
		 *                        - scan_nat_page
		 *                         - add_free_nid
		 *                          - __lookup_nat_cache
		 *  - f2fs_add_link
		 *   - f2fs_init_inode_metadata
		 *    - f2fs_new_inode_page
		 *     - f2fs_new_node_page
		 *      - set_node_addr
		 *  - f2fs_alloc_nid_done
		 *   - __remove_nid_from_list(PREALLOC_NID)
		 *                         - __insert_nid_to_list(FREE_NID)
		 */
		ne = __lookup_nat_cache(nid);
		if (ne && (!get_nat_flag(ne, IS_CHECKPOINTED) || nat_get_blkaddr(ne) != NULL_ADDR)) 	goto err_out;

		e = __lookup_free_nid_list(nid);
		if (e)
		{
			if (e->state == FREE_NID)	ret = true;
			goto err_out;
		}
	}
	ret = true;
	err = __insert_free_nid(i);
err_out:
	if (update)
	{
		update_free_nid_bitmap(nid, ret, build);
		if (!build)		available_nids++;
	}
//	spin_unlock(&nid_list_lock);
	}
	radix_tree_preload_end();

	if (err)	kmem_cache_free(free_nid_slab, i);
	return ret;
}

void f2fs_nm_info::remove_free_nid(nid_t nid)
{
	//struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i;
	bool need_free = false;

	spin_lock(&nid_list_lock);
	// FOR debug
	//free_nid* ii = new free_nid;
	//free_nid_root.insert(std::make_pair(3, ii));

	i = __lookup_free_nid_list(nid);
	if (i && i->state == FREE_NID) 
	{
		__remove_free_nid(i, FREE_NID);
		need_free = true;
	}
	spin_unlock(&nid_list_lock);

	if (need_free)	kmem_cache_free(free_nid_slab, i);
}


//static int scan_nat_page(f2fs_sb_info *sbi,	struct page *nat_page, nid_t start_nid)
int f2fs_nm_info::scan_nat_page(page* nat_page, nid_t start_nid)
{
//	struct f2fs_nm_info *nm_i = NM_I(sbi);
	f2fs_nat_block *nat_blk = page_address<f2fs_nat_block>(nat_page);
	block_t blk_addr;
	unsigned int nat_ofs = NAT_BLOCK_OFFSET(start_nid);
	int i;

	__set_bit_le(nat_ofs, nat_block_bitmap);

	i = start_nid % NAT_ENTRY_PER_BLOCK;

	for (; i < NAT_ENTRY_PER_BLOCK; i++, start_nid++)
	{
		if (unlikely(start_nid >= max_nid))		break;

		blk_addr = le32_to_cpu(nat_blk->entries[i].block_addr);
		if (blk_addr == NEW_ADDR)		return -EINVAL;
		if (blk_addr == NULL_ADDR)		// 地址0表示没有使用的nid
		{
			add_free_nid(start_nid, true, true);
		} 
		else
		{	auto_lock<spin_locker> lock(nid_list_lock);
//			spin_lock(&nid_list_lock);
			update_free_nid_bitmap(start_nid, false, true);
//			spin_unlock(&nid_list_lock);
		}
	}

	return 0;
}
static void scan_curseg_cache(f2fs_sb_info *sbi)
{
	f2fs_nm_info *nm_i = NM_I(sbi);
	curseg_info *curseg = sbi->CURSEG_I(CURSEG_HOT_DATA);
	f2fs_journal *journal = &curseg->journal;
	int i;

	down_read(&curseg->journal_rwsem);
	for (i = 0; i < nats_in_cursum(journal); i++) 
	{
		block_t addr;
		nid_t nid;

		addr = le32_to_cpu(nat_in_journal(journal, i).block_addr);
		nid = le32_to_cpu(nid_in_journal(journal, i));
		if (addr == NULL_ADDR)		nm_i->add_free_nid(nid, true, false);
		else						nm_i->remove_free_nid(nid);
	}
	up_read(&curseg->journal_rwsem);
}

static void scan_free_nid_bits(f2fs_sb_info *sbi)
{
	f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned int i;
	nid_t nid;

	down_read(&nm_i->nat_tree_lock);

	for (i = 0; i < nm_i->nat_blocks; i++) {
		if (!test_bit_le(i, nm_i->nat_block_bitmap))
			continue;
		if (!nm_i->free_nid_count[i])
			continue;
		for (UINT64 idx = 0; idx < NAT_ENTRY_PER_BLOCK; idx++) 
		{
			idx = find_next_bit_le(nm_i->free_nid_bitmap[i], NAT_ENTRY_PER_BLOCK, idx);
			if (idx >= NAT_ENTRY_PER_BLOCK)		break;

			nid = i * NAT_ENTRY_PER_BLOCK + idx;
			nm_i->add_free_nid(nid, true, false);

			if (nm_i->nid_cnt[FREE_NID] >= MAX_FREE_NIDS)		goto out;
		}
	}
out:
	scan_curseg_cache(sbi);

	up_read(&nm_i->nat_tree_lock);
}

//static int __f2fs_build_free_nids(struct f2fs_sb_info* sbi, bool sync, bool mount)
int f2fs_nm_info::__f2fs_build_free_nids(bool sync, bool mount)
{
//	struct f2fs_nm_info *nm_i = NM_I(sbi);
	int i = 0, ret;
	nid_t nid = next_scan_nid;

	if (unlikely(nid >= max_nid))	nid = 0;
	if (unlikely(nid % NAT_ENTRY_PER_BLOCK))	nid = NAT_BLOCK_OFFSET(nid) * NAT_ENTRY_PER_BLOCK;

	/* Enough entries */
	if (nid_cnt[FREE_NID] >= NAT_ENTRY_PER_BLOCK) 		return 0;
	if (!sync && !f2fs_available_free_memory(m_sbi, FREE_NIDS))			return 0;

	if (!mount) 
	{	/* try to find free nids in free_nid_bitmap */
		scan_free_nid_bits(m_sbi);
		if (nid_cnt[FREE_NID] >= NAT_ENTRY_PER_BLOCK)			return 0;
	}

	/* readahead nat pages to be scanned */
	// 从next_scan_nid开始，读取FREE_NID_PAGES (8)，读取结果保存在相应的page cache中
	m_sbi->f2fs_ra_meta_pages( NAT_BLOCK_OFFSET(nid), FREE_NID_PAGES, META_NAT, true);

	{ auto_lock<semaphore_read_lock> lock(nat_tree_lock);
		//down_read(&nat_tree_lock);

		while (1)
		{
			if (!test_bit_le(NAT_BLOCK_OFFSET(nid), nat_block_bitmap))
			{
				// 从cache的page中获取相应的page，通过nid转换为page index
				struct page* page = get_current_nat_page(m_sbi, nid);
				if (IS_ERR(page)) { ret = PTR_ERR(page); }
				else
				{// 扫描并且从page中提取nat entry，从nid的offset开始
					ret = scan_nat_page(page, nid);
					f2fs_put_page(page, 1);
				}

				if (ret)
				{
					//up_read(&nat_tree_lock);
					//f2fs_err(m_sbi, L"NAT is corrupt, run fsck to fix it");
					LOG_ERROR(L"[err] NAT is corrupt, run fsck to fix it");
					return ret;
				}
			}

			nid += (NAT_ENTRY_PER_BLOCK - (nid % NAT_ENTRY_PER_BLOCK));
			if (unlikely(nid >= max_nid))			nid = 0;
			if (++i >= FREE_NID_PAGES)			break;
		}
		/* go to the next free nat pages to find free nids abundantly */
		next_scan_nid = nid;
		/* find free nids from current sum_pages */
		scan_curseg_cache(m_sbi);
//		up_read(&nat_tree_lock);
	}
	m_sbi->f2fs_ra_meta_pages( NAT_BLOCK_OFFSET(next_scan_nid),	ra_nid_pages, META_NAT, false);

	return 0;
}

int f2fs_nm_info::f2fs_build_free_nids(bool sync, bool mount)
{
	int ret;
	LOG_DEBUG(L"[build_lock] lock");
	auto_lock<mutex_locker> lock(build_lock);
	//mutex_lock(&NM_I(this)->build_lock);
	ret = __f2fs_build_free_nids(sync, mount);
	//mutex_unlock(&NM_I(this)->build_lock);
	LOG_DEBUG(L"[build_lock] release");
	return ret;
}

/* If this function returns success, caller can obtain a new nid from second parameter of this function.
 * The returned nid could be used ino as well as nid when inode is created. */
// 申请新的nid，如果成功，在nid参数中返回。如果时inode则nid用作ino.
// 在free_nid_list中提取第一个entry，返回其nid,
//bool f2fs_alloc_nid(f2fs_sb_info *sbi, nid_t *nid)
bool f2fs_nm_info::f2fs_alloc_nid(OUT nid_t *nid)
{
//	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i = NULL;
retry:
	if (time_to_inject(m_sbi, FAULT_ALLOC_NID))
	{
		f2fs_show_injection_info(sbi, FAULT_ALLOC_NID);
		return false;
	}

	{auto_lock<spin_locker> lock(nid_list_lock);	//		spin_lock(&nid_list_lock);
		if (unlikely(available_nids == 0))
		{
			LOG_ERROR(L"[err] available nids = 0");
			return false;
		}

		/* We should not use stale free nids created by f2fs_build_free_nids */
//		if (nid_cnt[FREE_NID] && !on_f2fs_build_free_nids(this))
		LOG_DEBUG(L"[build_lock] check lock");
		if (nid_cnt[FREE_NID] && !mutex_is_locked(&build_lock))
		{
			LOG_DEBUG(L"[build_lock] check result not locked");
//			f2fs_bug_on(sbi, list_empty(&nm_i->free_nid_list));
			JCASSERT(!list_empty(&free_nid_list));
			i = list_first_entry(&free_nid_list, free_nid, list);
			*nid = i->nid;
			__move_free_nid(i, FREE_NID, PREALLOC_NID);
			available_nids--;
			update_free_nid_bitmap(*nid, false, false);
//			spin_unlock(&nm_i->nid_list_lock);
			return true;
		}
//		spin_unlock(&nm_i->nid_list_lock);
	}
	/* Let's scan nat pages and its caches to get free nids */
	if (!f2fs_build_free_nids(true, false))  goto retry;
	return false;
}

/* f2fs_alloc_nid() should be called prior to this function. */
//void f2fs_alloc_nid_done(struct f2fs_sb_info *sbi, nid_t nid)
void f2fs_nm_info::f2fs_alloc_nid_done(nid_t nid)
{
//	struct f2fs_nm_info *nm_i = NM_I(sbi);
	free_nid *i;
	{ auto_lock<spin_locker> lock(nid_list_lock);
//		spin_lock(&nm_i->nid_list_lock);
		i = __lookup_free_nid_list(nid);
		//f2fs_bug_on(sbi, !i);
		JCASSERT(i);
		__remove_free_nid(i, PREALLOC_NID);
//		spin_unlock(&nm_i->nid_list_lock);
	}
	kmem_cache_free(free_nid_slab, i);
}


#if 0


/*
 * f2fs_alloc_nid() should be called prior to this function.
 */
void f2fs_alloc_nid_failed(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i;
	bool need_free = false;

	if (!nid)
		return;

	spin_lock(&nm_i->nid_list_lock);
	i = __lookup_free_nid_list(nm_i, nid);
	f2fs_bug_on(sbi, !i);

	if (!f2fs_available_free_memory(sbi, FREE_NIDS)) {
		__remove_free_nid(sbi, i, PREALLOC_NID);
		need_free = true;
	} else {
		__move_free_nid(sbi, i, PREALLOC_NID, FREE_NID);
	}

	nm_i->available_nids++;

	update_free_nid_bitmap(sbi, nid, true, false);

	spin_unlock(&nm_i->nid_list_lock);

	if (need_free)
		kmem_cache_free(free_nid_slab, i);
}

int f2fs_try_to_free_nids(struct f2fs_sb_info *sbi, int nr_shrink)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	int nr = nr_shrink;

	if (nm_i->nid_cnt[FREE_NID] <= MAX_FREE_NIDS)
		return 0;

	if (!mutex_trylock(&nm_i->build_lock))
		return 0;

	while (nr_shrink && nm_i->nid_cnt[FREE_NID] > MAX_FREE_NIDS) {
		struct free_nid *i, *next;
		unsigned int batch = SHRINK_NID_BATCH_SIZE;

		spin_lock(&nm_i->nid_list_lock);
		list_for_each_entry_safe(i, next, &nm_i->free_nid_list, list) {
			if (!nr_shrink || !batch ||
				nm_i->nid_cnt[FREE_NID] <= MAX_FREE_NIDS)
				break;
			__remove_free_nid(sbi, i, FREE_NID);
			kmem_cache_free(free_nid_slab, i);
			nr_shrink--;
			batch--;
		}
		spin_unlock(&nm_i->nid_list_lock);
	}

	mutex_unlock(&nm_i->build_lock);

	return nr - nr_shrink;
}
#endif
int f2fs_recover_inline_xattr(struct inode *inode, struct page *page)
{
	void *src_addr, *dst_addr;
	size_t inline_size;
	struct page *ipage;
	struct f2fs_inode *ri;

	ipage = F2FS_I_SB(inode)->f2fs_get_node_page(inode->i_ino);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	ri = F2FS_INODE(page);
	if (ri->i_inline & F2FS_INLINE_XATTR) {
		if (!f2fs_has_inline_xattr(inode)) {
			F2FS_I(inode)->set_inode_flag(FI_INLINE_XATTR);
			stat_inc_inline_xattr(inode);
		}
	} else {
		if (f2fs_has_inline_xattr(inode)) {
			stat_dec_inline_xattr(inode);
			clear_inode_flag(F2FS_I(inode), FI_INLINE_XATTR);
		}
		goto update_inode;
	}

	dst_addr = inline_xattr_addr(inode, ipage);
	src_addr = inline_xattr_addr(inode, page);
	inline_size = inline_xattr_size(inode);

	f2fs_wait_on_page_writeback(ipage, NODE, true, true);
	memcpy(dst_addr, src_addr, inline_size);
update_inode:
	F2FS_I(inode)->f2fs_update_inode(ipage);
	f2fs_put_page(ipage, 1);
	return 0;
}

int f2fs_recover_xattr_data(struct inode *inode, struct page *page)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	nid_t prev_xnid = F2FS_I(inode)->i_xattr_nid;
	nid_t new_xnid;
	struct dnode_of_data dn;
	struct node_info ni;
	struct page *xpage;
	int err;

	if (!prev_xnid)
		goto recover_xnid;

	/* 1: invalidate the previous xattr nid */
	err = NM_I(sbi)->f2fs_get_node_info( prev_xnid, &ni);
	if (err)
		return err;

	f2fs_invalidate_blocks(sbi, ni.blk_addr);
	sbi->dec_valid_node_count(F2FS_I(inode), false);
	set_node_addr(sbi, &ni, NULL_ADDR, false);

recover_xnid:
	/* 2: update xattr nid in inode */
	if (!NM_I(sbi)->f2fs_alloc_nid(&new_xnid))
		return -ENOSPC;

	dn.set_new_dnode(inode, NULL, NULL, new_xnid);
	xpage = dn.inode->f2fs_new_node_page(&dn, XATTR_NODE_OFFSET);
	if (IS_ERR(xpage)) {
		f2fs_alloc_nid_failed(sbi, new_xnid);
		return PTR_ERR(xpage);
	}

	sbi->nm_info->f2fs_alloc_nid_done(new_xnid);
	F2FS_I(inode)->f2fs_update_inode_page();

	/* 3: update and set xattr node page dirty */
	memcpy(F2FS_NODE(xpage), F2FS_NODE(page), VALID_XATTR_BLOCK_SIZE);

	set_page_dirty(xpage);
	f2fs_put_page(xpage, 1);

	return 0;
}
int f2fs_recover_inode_page(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_inode *src, *dst;
	nid_t ino = ino_of_node(page);
	struct node_info old_ni, new_ni;
	struct page *ipage;
	int err;

	err = NM_I(sbi)->f2fs_get_node_info( ino, &old_ni);
	if (err)
		return err;

	if (unlikely(old_ni.blk_addr != NULL_ADDR))
		return -EINVAL;
retry:
	ipage = f2fs_grab_cache_page(NODE_MAPPING(sbi), ino, false);
	if (!ipage) 
	{
//		congestion_wait(BLK_RW_ASYNC, DEFAULT_IO_TIMEOUT);
		goto retry;
	}

	/* Should not use this inode from free nid list */
	sbi->nm_info->remove_free_nid(ino);

	if (!PageUptodate(ipage))		SetPageUptodate(ipage);
	fill_node_footer(ipage, ino, ino, 0, true);
	set_cold_node(ipage, false);

	src = F2FS_INODE(page);
	dst = F2FS_INODE(ipage);

	memcpy(dst, src, offsetof(struct f2fs_inode, i_ext));
	dst->i_size = 0;
	dst->i_blocks = cpu_to_le64(1);
	dst->i_links = cpu_to_le32(1);
	dst->i_xattr_nid = 0;
	dst->i_inline = src->i_inline & (F2FS_INLINE_XATTR | F2FS_EXTRA_ATTR);
	if (dst->i_inline & F2FS_EXTRA_ATTR) {
		dst->_u._s.i_extra_isize = src->_u._s.i_extra_isize;

		if (f2fs_sb_has_flexible_inline_xattr(sbi) &&
			F2FS_FITS_IN_INODE(src, le16_to_cpu(src->_u._s.i_extra_isize), _u._s.i_inline_xattr_size))
			dst->_u._s.i_inline_xattr_size = src->_u._s.i_inline_xattr_size;

		if (f2fs_sb_has_project_quota(sbi) &&
			F2FS_FITS_IN_INODE(src, le16_to_cpu(src->_u._s.i_extra_isize), _u._s.i_projid))
			dst->_u._s.i_projid = src->_u._s.i_projid;

		if (f2fs_sb_has_inode_crtime(sbi) &&
			F2FS_FITS_IN_INODE(src, le16_to_cpu(src->_u._s.i_extra_isize), _u._s.i_crtime_nsec))
		{
			dst->_u._s.i_crtime = src->_u._s.i_crtime;
			dst->_u._s.i_crtime_nsec = src->_u._s.i_crtime_nsec;
		}
	}

	new_ni = old_ni;
	new_ni.ino = ino;

	if (unlikely(sbi->inc_valid_node_count(NULL, true)))	WARN_ON(1);
	set_node_addr(sbi, &new_ni, NEW_ADDR, false);
	inc_valid_inode_count(sbi);
	set_page_dirty(ipage);
	f2fs_put_page(ipage, 1);
	return 0;
}

int f2fs_restore_node_summary(struct f2fs_sb_info *sbi,	unsigned int segno, struct f2fs_summary_block *sum)
{
	struct f2fs_node *rn;
	struct f2fs_summary *sum_entry;
	block_t addr;
	int i, idx, last_offset, nrpages;

	/* scan the node segment */
	last_offset = sbi->blocks_per_seg;
	addr = START_BLOCK(sbi, segno);
	sum_entry = &sum->entries[0];

	for (i = 0; i < last_offset; i += nrpages, addr += nrpages) {
		nrpages = bio_max_segs(last_offset - i);

		/* readahead node pages */
		sbi->f2fs_ra_meta_pages( addr, nrpages, META_POR, true);

		for (idx = addr; idx < addr + nrpages; idx++) {
			struct page *page = f2fs_get_tmp_page(sbi, idx);

			if (IS_ERR(page))			return (int)PTR_ERR(page);

			rn = F2FS_NODE(page);
			sum_entry->nid = rn->footer.nid;
			sum_entry->_u._s.version = 0;
			sum_entry->_u._s.ofs_in_node = 0;
			sum_entry++;
			f2fs_put_page(page, 1);
		}

		invalidate_mapping_pages(META_MAPPING(sbi), addr, addr + nrpages);
	}
	return 0;
}

static void remove_nats_in_journal(f2fs_sb_info *sbi)
{
	f2fs_nm_info *nm_i = NM_I(sbi);
	curseg_info *curseg = sbi->CURSEG_I( CURSEG_HOT_DATA);
	f2fs_journal *journal = &curseg->journal;
	int i;

	down_write(&curseg->journal_rwsem);
	for (i = 0; i < nats_in_cursum(journal); i++) {
		struct nat_entry *ne;
		struct f2fs_nat_entry raw_ne;
		nid_t nid = le32_to_cpu(nid_in_journal(journal, i));

		if (f2fs_check_nid_range(sbi, nid))
			continue;

		raw_ne = nat_in_journal(journal, i);

		ne = nm_i->__lookup_nat_cache(nid);
		if (!ne) {
			ne = __alloc_nat_entry(nid, true);
			nm_i->__init_nat_entry( ne, &raw_ne, true);
		}

		/*
		 * if a free nat in journal has not been used after last
		 * checkpoint, we should remove it from available nids,
		 * since later we will add it again.
		 */
		if (!get_nat_flag(ne, IS_DIRTY) &&
				le32_to_cpu(raw_ne.block_addr) == NULL_ADDR) {
			spin_lock(&nm_i->nid_list_lock);
			nm_i->available_nids--;
			spin_unlock(&nm_i->nid_list_lock);
		}

		__set_nat_cache_dirty(nm_i, ne);
	}
	update_nats_in_cursum(journal, -i);
	up_write(&curseg->journal_rwsem);
}

static void __adjust_nat_entry_set(struct nat_entry_set *nes, struct list_head *head, int max)
{
	struct nat_entry_set *cur;

	if (nes->entry_cnt >= max)
		goto add_out;

	list_for_each_entry(nat_entry_set, cur, head, set_list) 
	{
		if (cur->entry_cnt >= nes->entry_cnt)
		{
			list_add(&nes->set_list, cur->set_list.prev);
			return;
		}
	}
add_out:
	list_add_tail(&nes->set_list, head);
}

//static void __update_nat_bits(struct f2fs_sb_info *sbi, nid_t start_nid, struct page *page)
void f2fs_nm_info::__update_nat_bits(nid_t start_nid, page* page)
{
//	struct f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned int nat_index = start_nid / NAT_ENTRY_PER_BLOCK;
	struct f2fs_nat_block *nat_blk = page_address<f2fs_nat_block>(page);
	int valid = 0;
	int i = 0;

	if (!enabled_nat_bits(m_sbi, NULL))
		return;

	if (nat_index == 0) {
		valid = 1;
		i = 1;
	}
	for (; i < NAT_ENTRY_PER_BLOCK; i++) {
		if (le32_to_cpu(nat_blk->entries[i].block_addr) != NULL_ADDR)
			valid++;
	}
	if (valid == 0) {
		__set_bit_le(nat_index, empty_nat_bits);
		__clear_bit_le(nat_index, full_nat_bits);
		return;
	}

	__clear_bit_le(nat_index, empty_nat_bits);
	if (valid == NAT_ENTRY_PER_BLOCK)
		__set_bit_le(nat_index, full_nat_bits);
	else
		__clear_bit_le(nat_index, full_nat_bits);
}


static int __flush_nat_entry_set(f2fs_sb_info *sbi, nat_entry_set *set, cp_control *cpc)
{
	curseg_info *curseg = sbi->CURSEG_I( CURSEG_HOT_DATA);
	f2fs_journal *journal = &curseg->journal;
	nid_t start_nid = set->set * NAT_ENTRY_PER_BLOCK;
	bool to_journal = true;
	struct f2fs_nat_block *nat_blk=NULL;
	struct nat_entry *ne, *cur;
	struct page *page = NULL;

	/*
	 * there are two steps to flush nat entries:
	 * #1, flush nat entries to journal in current hot data summary block.
	 * #2, flush nat entries to nat page.
	 */
	if (enabled_nat_bits(sbi, cpc) ||
		!__has_cursum_space(journal, set->entry_cnt, NAT_JOURNAL))
		to_journal = false;

	if (to_journal) {
		down_write(&curseg->journal_rwsem);
	} else {
		page = get_next_nat_page(sbi, start_nid);
		if (IS_ERR(page))		return PTR_ERR(page);

		nat_blk = page_address<f2fs_nat_block>(page);
		f2fs_bug_on(sbi, !nat_blk);
	}

	/* flush dirty nats in nat entry set */
	f2fs_nm_info* nmi = NM_I(sbi);
	list_for_each_entry_safe(nat_entry, ne, cur, &set->entry_list, list) 
	{
		struct f2fs_nat_entry *raw_ne;
		nid_t nid = nat_get_nid(ne);
		int offset;

		f2fs_bug_on(sbi, nat_get_blkaddr(ne) == NEW_ADDR);

		if (to_journal) {
			offset = f2fs_lookup_journal_in_cursum(journal, NAT_JOURNAL, nid, 1);
			f2fs_bug_on(sbi, offset < 0);
			raw_ne = &nat_in_journal(journal, offset);
			nid_in_journal(journal, offset) = cpu_to_le32(nid);
		} else {
			raw_ne = &nat_blk->entries[nid - start_nid];
		}
		raw_nat_from_node_info(raw_ne, &ne->ni);
		nat_reset_flag(ne);
		__clear_nat_cache_dirty(NM_I(sbi), set, ne);
		if (nat_get_blkaddr(ne) == NULL_ADDR) 
		{
			nmi->add_free_nid(nid, false, true);
		}
		else
		{
			spin_lock(&nmi->nid_list_lock);
			nmi->update_free_nid_bitmap(nid, false, false);
			spin_unlock(&NM_I(sbi)->nid_list_lock);
		}
	}

	if (to_journal)
	{
		up_write(&curseg->journal_rwsem);
	} else {
		nmi->__update_nat_bits(start_nid, page);
		f2fs_put_page(page, 1);
	}

	/* Allow dirty nats by node block allocation in write_begin */
	if (!set->entry_cnt) {
		radix_tree_delete<nat_entry_set>(&nmi->nat_set_root, set->set);
		kmem_cache_free(nat_entry_set_slab, set);
	}
	return 0;
}

/* This function is called during the checkpointing process. */
int f2fs_flush_nat_entries(f2fs_sb_info *sbi, cp_control *cpc)
{
	f2fs_nm_info *nm_i = NM_I(sbi);
	struct curseg_info *curseg = sbi->CURSEG_I( CURSEG_HOT_DATA);
	f2fs_journal *journal = &curseg->journal;
	struct nat_entry_set *setvec[SETVEC_SIZE];
	struct nat_entry_set *set, *tmp;
	unsigned int found;
	nid_t set_idx = 0;
	LIST_HEAD(sets);
	int err = 0;

	/*
	 * during unmount, let's flush nat_bits before checking
	 * nat_cnt[DIRTY_NAT].
	 */
	if (enabled_nat_bits(sbi, cpc)) {
		down_write(&nm_i->nat_tree_lock);
		remove_nats_in_journal(sbi);
		up_write(&nm_i->nat_tree_lock);
	}

	if (!nm_i->nat_cnt[DIRTY_NAT])
		return 0;

	down_write(&nm_i->nat_tree_lock);

	/*
	 * if there are no enough space in journal to store dirty nat
	 * entries, remove all entries from journal and merge them
	 * into nat entry set.
	 */
	if (enabled_nat_bits(sbi, cpc) ||
		!__has_cursum_space(journal,
			nm_i->nat_cnt[DIRTY_NAT], NAT_JOURNAL))
		remove_nats_in_journal(sbi);

	while ((found = __gang_lookup_nat_set(nm_i,
					set_idx, SETVEC_SIZE, setvec))) {
		unsigned idx;

		set_idx = setvec[found - 1]->set + 1;
		for (idx = 0; idx < found; idx++)
			__adjust_nat_entry_set(setvec[idx], &sets,
						MAX_NAT_JENTRIES(journal));
	}

	/* flush dirty nats in nat entry set */
	list_for_each_entry_safe(nat_entry_set, set, tmp, &sets, set_list)
	{
		err = __flush_nat_entry_set(sbi, set, cpc);
		if (err)		break;
	}

	up_write(&nm_i->nat_tree_lock);
	/* Allow dirty nats by node block allocation in write_begin */

	return err;
}

//static int __get_nat_bitmaps(struct f2fs_sb_info *sbi)
int f2fs_nm_info::__get_nat_bitmaps(void)
{
	struct f2fs_checkpoint *ckpt = m_sbi->F2FS_CKPT();
//	struct f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned int nat_bits_bytes = nat_blocks / BITS_PER_BYTE;
	unsigned int i;
	__u64 cp_ver = cur_cp_version(ckpt);
	block_t nat_bits_addr;

	if (!enabled_nat_bits(m_sbi, NULL))	return 0;

	nat_bits_blocks = F2FS_BLK_ALIGN((nat_bits_bytes << 1) + 8);
	nat_bits = f2fs_kvzalloc<BYTE>(m_sbi,	nat_bits_blocks << F2FS_BLKSIZE_BITS/*, GFP_KERNEL*/);
	if (!nat_bits) return -ENOMEM;

	nat_bits_addr = m_sbi->__start_cp_addr() + m_sbi->blocks_per_seg - nat_bits_blocks;
	for (i = 0; i < nat_bits_blocks; i++) 
	{
		struct page *page;

		page = m_sbi->f2fs_get_meta_page(nat_bits_addr++);
		if (IS_ERR(page))		return PTR_ERR(page);

		memcpy(nat_bits + (i << F2FS_BLKSIZE_BITS), page_address<BYTE>(page), F2FS_BLKSIZE);
		f2fs_put_page(page, 1);
	}

	cp_ver |= (cur_cp_crc(ckpt) << 32);
	if (cpu_to_le64(cp_ver) != *(__le64 *)nat_bits)
	{
		disable_nat_bits(true);
		return 0;
	}

	full_nat_bits = nat_bits + 8;
	empty_nat_bits = full_nat_bits + nat_bits_bytes;

	f2fs_notice(m_sbi, L"Found nat_bits in checkpoint", 0);
	return 0;
}

void f2fs_sb_info::load_free_nid_bitmap(void)
{
	struct f2fs_nm_info *nm_i = NM_I(this);
//	unsigned int i = 0;
	nid_t nid, last_nid;

	if (!enabled_nat_bits(this, NULL))		return;

	for (UINT64 i = 0; i < nm_i->nat_blocks; i++) 
	{
		i = find_next_bit_le(nm_i->empty_nat_bits, nm_i->nat_blocks, i);
		if (i >= nm_i->nat_blocks)		break;

		__set_bit_le(i, nm_i->nat_block_bitmap);

		nid = i * NAT_ENTRY_PER_BLOCK;
		last_nid = nid + NAT_ENTRY_PER_BLOCK;

		spin_lock(&NM_I(this)->nid_list_lock);
		for (; nid < last_nid; nid++)		nm_i->update_free_nid_bitmap(nid, true, true);
		spin_unlock(&NM_I(this)->nid_list_lock);
	}

	for (UINT64 i = 0; i < nm_i->nat_blocks; i++)
	{
		i = find_next_bit_le(nm_i->full_nat_bits, nm_i->nat_blocks, i);
		if (i >= nm_i->nat_blocks)			break;
		__set_bit_le(i, nm_i->nat_block_bitmap);
	}
}

/*
int f2fs_sb_info::init_node_manager(void)
{
	struct f2fs_super_block *sb_raw = F2FS_RAW_SUPER();
	struct f2fs_nm_info *nm_i = NM_I(this);
	char *version_bitmap;
	unsigned int nat_segs;
	int err;
	//<YUAN>
	nm_i->m_sbi = this;

	nm_i->nat_blkaddr = le32_to_cpu(sb_raw->nat_blkaddr);

	// segment_count_nat includes pair segment so divide to 2. 
	nat_segs = le32_to_cpu(sb_raw->segment_count_nat) >> 1;
	nm_i->nat_blocks = nat_segs << le32_to_cpu(sb_raw->log_blocks_per_seg);
	nm_i->max_nid = NAT_ENTRY_PER_BLOCK * nm_i->nat_blocks;

	// not used nids: 0, node, meta, (and root counted as valid node) 
	nm_i->available_nids = nm_i->max_nid - this->total_valid_node_count - F2FS_RESERVED_NODE_NUM;
	nm_i->nid_cnt[FREE_NID] = 0;
	nm_i->nid_cnt[PREALLOC_NID] = 0;
	nm_i->ram_thresh = DEF_RAM_THRESHOLD;
	nm_i->ra_nid_pages = DEF_RA_NID_PAGES;
	nm_i->dirty_nats_ratio = DEF_DIRTY_NAT_RATIO_THRESHOLD;

	INIT_RADIX_TREE(&nm_i->free_nid_root, GFP_ATOMIC);
	INIT_LIST_HEAD(&nm_i->free_nid_list);
	INIT_RADIX_TREE(&nm_i->nat_root, GFP_NOIO);
	INIT_RADIX_TREE(&nm_i->nat_set_root, GFP_NOIO);
	INIT_LIST_HEAD(&nm_i->nat_entries);
	spin_lock_init(&nm_i->nat_list_lock);

	mutex_init(&nm_i->build_lock);
	spin_lock_init(&nm_i->nid_list_lock);
	init_rwsem(&nm_i->nat_tree_lock);

	nm_i->next_scan_nid = le32_to_cpu(this->ckpt->next_free_nid);
	nm_i->bitmap_size = __bitmap_size(NAT_BITMAP);
	version_bitmap = (char*)__bitmap_ptr(NAT_BITMAP);
	nm_i->nat_bitmap = kmemdup(version_bitmap, nm_i->bitmap_size, GFP_KERNEL);
	if (!nm_i->nat_bitmap)	return -ENOMEM;

	err = __get_nat_bitmaps(this);
	if (err) return err;

#ifdef CONFIG_F2FS_CHECK_FS
	nm_i->nat_bitmap_mir = kmemdup(version_bitmap, nm_i->bitmap_size, GFP_KERNEL);
	if (!nm_i->nat_bitmap_mir) return -ENOMEM;
#endif

	return 0;
}
*/

int f2fs_sb_info::init_free_nid_cache(void)
{
	struct f2fs_nm_info *nm_i = NM_I(this);
	//int i;

//	nm_i->free_nid_bitmap =	f2fs_kvzalloc<BYTE>(this, array_size(sizeof(unsigned char *), nm_i->nat_blocks), /*GFP_KERNEL*/);
	nm_i->free_nid_bitmap =	f2fs_kvzalloc<BYTE*>(this, nm_i->nat_blocks /*,GFP_KERNEL*/);
	if (!nm_i->free_nid_bitmap)		return -ENOMEM;

	for (UINT i = 0; i < nm_i->nat_blocks; i++) 
	{
		nm_i->free_nid_bitmap[i] = f2fs_kvzalloc<BYTE>(this, f2fs_bitmap_size(NAT_ENTRY_PER_BLOCK)/*, GFP_KERNEL*/);
		if (!nm_i->free_nid_bitmap[i])		return -ENOMEM;
	}

	nm_i->nat_block_bitmap = f2fs_kvzalloc<BYTE>(this, nm_i->nat_blocks / 8/*,GFP_KERNEL*/);
	if (!nm_i->nat_block_bitmap)	return -ENOMEM;

	nm_i->free_nid_count =	f2fs_kvzalloc<unsigned short>(this, nm_i->nat_blocks/*, GFP_KERNEL*/);
	if (!nm_i->free_nid_count)	return -ENOMEM;
	return 0;
}


int f2fs_sb_info::f2fs_build_node_manager(void)
{
	int err;
//	nm_info = f2fs_kzalloc<f2fs_nm_info>(this, 1/*, GFP_KERNEL*/);
	JCASSERT(nm_info == NULL);
	nm_info = new f2fs_nm_info(this);	// <YUAN> 由于f2fs_nm_info包含 std::map结构，不能清零
	if (!nm_info)		THROW_ERROR(ERR_MEM, L"failed on creating f2fs_nm_info");
		//return -ENOMEM;

//	err = init_node_manager();
//	if (err) return err;

	err = init_free_nid_cache();
	if (err) return err;

	/* load free nid status from nat_bits table */
	load_free_nid_bitmap();
	return nm_info->f2fs_build_free_nids(true, true);
}

void f2fs_sb_info::f2fs_destroy_node_manager()
{
#if 0 //移植到f2fs_nm_info()的析构函数中
	f2fs_nm_info *nm_i = NM_I(sbi);
	free_nid *i, *next_i;
	struct nat_entry *natvec[NATVEC_SIZE];
	struct nat_entry_set *setvec[SETVEC_SIZE];
	nid_t nid = 0;
	unsigned int found;

	if (!nm_i)
		return;

	/* destroy free nid list */
	spin_lock(&nm_i->nid_list_lock);
	list_for_each_entry_safe(free_nid, i, next_i, &nm_i->free_nid_list, list) 
	{
		nm_i->__remove_free_nid(i, FREE_NID);
		spin_unlock(&nm_i->nid_list_lock);
		kmem_cache_free(free_nid_slab, i);
		spin_lock(&nm_i->nid_list_lock);
	}
	f2fs_bug_on(sbi, nm_i->nid_cnt[FREE_NID]);
	f2fs_bug_on(sbi, nm_i->nid_cnt[PREALLOC_NID]);
	f2fs_bug_on(sbi, !list_empty(&nm_i->free_nid_list));
	spin_unlock(&nm_i->nid_list_lock);

	/* destroy nat cache */
	down_write(&nm_i->nat_tree_lock);
	while ((found = __gang_lookup_nat_cache(nm_i, nid, NATVEC_SIZE, natvec))) 
	{
		unsigned idx;

		nid = nat_get_nid(natvec[found - 1]) + 1;
		for (idx = 0; idx < found; idx++) {
			spin_lock(&nm_i->nat_list_lock);
			list_del(&natvec[idx]->list);
			spin_unlock(&nm_i->nat_list_lock);

			__del_from_nat_cache(nm_i, natvec[idx]);
		}
	}
	f2fs_bug_on(sbi, nm_i->nat_cnt[TOTAL_NAT]);

	/* destroy nat set cache */
	nid = 0;
	while ((found = __gang_lookup_nat_set(nm_i, nid, SETVEC_SIZE, setvec))) 
	{
		unsigned idx;

		nid = setvec[found - 1]->set + 1;
		for (idx = 0; idx < found; idx++) {
			/* entry_cnt is not zero, when cp_error was occurred */
			f2fs_bug_on(sbi, !list_empty(&setvec[idx]->entry_list));
			radix_tree_delete<nat_entry_set>(&nm_i->nat_set_root, setvec[idx]->set);
			kmem_cache_free(nat_entry_set_slab, setvec[idx]);
		}
	}
	up_write(&nm_i->nat_tree_lock);

	f2fs_kvfree(nm_i->nat_block_bitmap);	//在init_free_nid()中alloc

	if (nm_i->free_nid_bitmap) 
	{
		int i;
		for (i = 0; i < nm_i->nat_blocks; i++)
		{
			f2fs_kvfree(nm_i->free_nid_bitmap[i]);
		}
		f2fs_kvfree(nm_i->free_nid_bitmap);

	}
	f2fs_kvfree(nm_i->free_nid_count);

	f2fs_kvfree(nm_i->nat_bitmap);
	f2fs_kvfree(nm_i->nat_bits);
#ifdef CONFIG_F2FS_CHECK_FS
	kvfree(nm_i->nat_bitmap_mir);
#endif

#endif
	delete nm_info;
	nm_info = NULL;
//	f2fs_kfree(nm_i);
//	delete nm_i;	//在f2fs_sb_info::f2fs_build_node_manager()中alloc
}
#if 0


int __init f2fs_create_node_manager_caches(void)
{
	nat_entry_slab = f2fs_kmem_cache_create("f2fs_nat_entry",
			sizeof(struct nat_entry));
	if (!nat_entry_slab)
		goto fail;

	free_nid_slab = f2fs_kmem_cache_create("f2fs_free_nid",
			sizeof(struct free_nid));
	if (!free_nid_slab)
		goto destroy_nat_entry;

	nat_entry_set_slab = f2fs_kmem_cache_create("f2fs_nat_entry_set",
			sizeof(struct nat_entry_set));
	if (!nat_entry_set_slab)
		goto destroy_free_nid;

	fsync_node_entry_slab = f2fs_kmem_cache_create("f2fs_fsync_node_entry",
			sizeof(struct fsync_node_entry));
	if (!fsync_node_entry_slab)
		goto destroy_nat_entry_set;
	return 0;

destroy_nat_entry_set:
	kmem_cache_destroy(nat_entry_set_slab);
destroy_free_nid:
	kmem_cache_destroy(free_nid_slab);
destroy_nat_entry:
	kmem_cache_destroy(nat_entry_slab);
fail:
	return -ENOMEM;
}

void f2fs_destroy_node_manager_caches(void)
{
	kmem_cache_destroy(fsync_node_entry_slab);
	kmem_cache_destroy(nat_entry_set_slab);
	kmem_cache_destroy(free_nid_slab);
	kmem_cache_destroy(nat_entry_slab);
}

#endif


f2fs_nm_info::f2fs_nm_info(f2fs_sb_info * sbi) : m_sbi(sbi), build_lock(NULL)
{
	JCASSERT(m_sbi);
	f2fs_super_block *sb_raw = m_sbi->F2FS_RAW_SUPER();

	nat_blkaddr = le32_to_cpu(sb_raw->nat_blkaddr);
	UINT32 nat_segs = le32_to_cpu(sb_raw->segment_count_nat) >> 1;
	nat_blocks = nat_segs << le32_to_cpu(sb_raw->log_blocks_per_seg);
	max_nid = NAT_ENTRY_PER_BLOCK * nat_blocks;

	/* not used nids: 0, node, meta, (and root counted as valid node) */
	available_nids = max_nid - m_sbi->total_valid_node_count - F2FS_RESERVED_NODE_NUM;
	nid_cnt[FREE_NID] = 0;
	nid_cnt[PREALLOC_NID] = 0;
	ram_thresh = DEF_RAM_THRESHOLD;
	ra_nid_pages = DEF_RA_NID_PAGES;
	dirty_nats_ratio = DEF_DIRTY_NAT_RATIO_THRESHOLD;

	INIT_RADIX_TREE(&free_nid_root, GFP_ATOMIC);
	INIT_LIST_HEAD(&free_nid_list);
	INIT_RADIX_TREE(&nat_root, GFP_NOIO);
	INIT_RADIX_TREE(&nat_set_root, GFP_NOIO);
	INIT_LIST_HEAD(&nat_entries);
	spin_lock_init(&nat_list_lock);

	LOG_DEBUG(L"[build_lock] init");
	mutex_init(&build_lock);
	spin_lock_init(&nid_list_lock);
	init_rwsem(&nat_tree_lock);

	next_scan_nid = le32_to_cpu(m_sbi->ckpt->next_free_nid);
	bitmap_size = m_sbi->__bitmap_size(NAT_BITMAP);
	BYTE * version_bitmap = m_sbi->__bitmap_ptr(NAT_BITMAP);
	nat_bitmap = kmemdup(version_bitmap, bitmap_size, GFP_KERNEL);
	if (!nat_bitmap)	THROW_ERROR(ERR_MEM, L"failed on creating bitmap, size=%d", bitmap_size);
		//return -ENOMEM;

	// 由于在__get_nat_bitmaps() -> disable_nat_bits()中会用到sbi->nm_info，需要先设置变量
//	sbi->nm_info = this;
	int err = __get_nat_bitmaps();
	if (err) THROW_ERROR(ERR_APP, L"failed on getting nat bitmap");
		//return err;

#ifdef CONFIG_F2FS_CHECK_FS
	nat_bitmap_mir = kmemdup(version_bitmap, bitmap_size, GFP_KERNEL);
	if (!nat_bitmap_mir) return -ENOMEM;
#endif
}

f2fs_nm_info::~f2fs_nm_info(void)
{
	free_nid* i, * next_i;
	struct nat_entry* natvec[NATVEC_SIZE];
	struct nat_entry_set* setvec[SETVEC_SIZE];
	nid_t nid = 0;
	unsigned int found;

//	if (!this)	return;

	/* destroy free nid list */
	spin_lock(&this->nid_list_lock);
	list_for_each_entry_safe(free_nid, i, next_i, &this->free_nid_list, list)
	{
		this->__remove_free_nid(i, FREE_NID);
		spin_unlock(&this->nid_list_lock);
		kmem_cache_free(free_nid_slab, i);
		spin_lock(&this->nid_list_lock);
	}
	f2fs_bug_on(m_sbi, this->nid_cnt[FREE_NID]);
	f2fs_bug_on(m_sbi, this->nid_cnt[PREALLOC_NID]);
	f2fs_bug_on(m_sbi, !list_empty(&this->free_nid_list));
	spin_unlock(&this->nid_list_lock);

	/* destroy nat cache */
	down_write(&this->nat_tree_lock);
	while ((found = __gang_lookup_nat_cache(this, nid, NATVEC_SIZE, natvec)))
	{
		unsigned idx;

		nid = nat_get_nid(natvec[found - 1]) + 1;
		for (idx = 0; idx < found; idx++) {
			spin_lock(&this->nat_list_lock);
			list_del(&natvec[idx]->list);
			spin_unlock(&this->nat_list_lock);

			__del_from_nat_cache(this, natvec[idx]);
		}
	}
	f2fs_bug_on(m_sbi, this->nat_cnt[TOTAL_NAT]);

	/* destroy nat set cache */
	nid = 0;
	while ((found = __gang_lookup_nat_set(this, nid, SETVEC_SIZE, setvec)))
	{
		unsigned idx;

		nid = setvec[found - 1]->set + 1;
		for (idx = 0; idx < found; idx++) {
			/* entry_cnt is not zero, when cp_error was occurred */
			f2fs_bug_on(m_sbi, !list_empty(&setvec[idx]->entry_list));
			radix_tree_delete<nat_entry_set>(&this->nat_set_root, setvec[idx]->set);
			kmem_cache_free(nat_entry_set_slab, setvec[idx]);
		}
	}
	up_write(&this->nat_tree_lock);

	f2fs_kvfree(this->nat_block_bitmap);	//在init_free_nid()中alloc

	if (this->free_nid_bitmap)
	{
		int i;
		for (i = 0; i < this->nat_blocks; i++)		f2fs_kvfree(this->free_nid_bitmap[i]);
		f2fs_kvfree(this->free_nid_bitmap);
	}
	f2fs_kvfree(this->free_nid_count);

	f2fs_kvfree(nat_bitmap);
	f2fs_kvfree(nat_bits);
#ifdef CONFIG_F2FS_CHECK_FS
	kvfree(this->nat_bitmap_mir);
#endif



//	f2fs_kvfree(nat_block_bitmap);
//	f2fs_kvfree(free_nid_count);

	//for (UINT i = 0; i < nat_blocks; i++)
	//{
	//	f2fs_kvfree(free_nid_bitmap[i]);
	//}
	//f2fs_kvfree(free_nid_bitmap);

	LOG_DEBUG(L"[build_lock] destory");
	mutex_destory(&build_lock);

}
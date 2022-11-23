///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include "../include/mm_types.h"
#include "../include/fs.h"
#include "../include/backing-dev.h"
#include "../include/pagemap.h"
#include "../include/page-manager.h"

LOCAL_LOGGER_ENABLE(L"linux.page_manager", LOGGER_LEVEL_DEBUGINFO);


CPageManager::CPageManager(size_t cache_size)
{
	m_cache_nr = cache_size;
	size_t mem_size = cache_size * PAGE_SIZE;
	m_cache = new page[cache_size];
	m_buffer = VirtualAlloc(0, mem_size, MEM_COMMIT, PAGE_READWRITE);

	for (size_t ii = 0; ii < cache_size; ++ii)
	{
		m_cache[ii].init(this, (BYTE*)m_buffer + ii * PAGE_SIZE);
		set_bit(PG_free, m_cache[ii].flags);
		m_free_list.push_back(m_cache + ii);
	}

	InitializeCriticalSection(&m_page_wait_lock);
	InitializeCriticalSection(&m_free_list_lock);
}

CPageManager::~CPageManager(void)
{
#ifdef _DEBUG
	LOG_ERROR(L"buf_size=%d, active=%d, inactive=%d, free=%d", m_cache_nr, m_active.size(), m_inactive.size(), m_free_list.size());
	JCASSERT(m_cache_nr == m_free_list.size());
#endif
	delete[] m_cache;
	VirtualFree(m_buffer, m_cache_nr * PAGE_SIZE, MEM_RELEASE);
	DeleteCriticalSection(&m_page_wait_lock);
	DeleteCriticalSection(&m_free_list_lock);
}

page* CPageManager::NewPage(void)
{
//	EnterCriticalSection(&m_free_list_lock);
	if (m_free_list.empty())
	{
		LeaveCriticalSection(&m_free_list_lock);
		THROW_ERROR(ERR_MEM, L"no enough pages");
	}

	page* pp = nullptr;
	while (1)	// retry
	{
		lock();
		for (auto it = m_free_list.begin(); it != m_free_list.end(); ++it)
		{
			if (!PageLocked(*it)) 
			{
				pp = *it;
				m_free_list.erase(it);
				break;
			}
		}
		unlock();
		if (pp) break;
	}

//	page* pp = m_free_list.front();
//	m_free_list.pop_front();
//	clear_bit(PG_free, pp->flags);
//	LeaveCriticalSection(&m_free_list_lock);
	// 重新初始化 page
	pp->reinit();
	F_LOG_DEBUG(L"page", L" page=%p, index=%lld, ref=%d, flag=%X, lock_th=%04X, new page", pp, pp-m_cache, pp->_refcount, pp->flags, pp->lock_th_id);
	return pp;
}

void CPageManager::DeletePage(page* pp)
{
	//JCASSERT(!PageLRU(pp));
	lock();
	if (PageLRU(pp))
	{
		if (PageActive(pp)) m_active.remove(pp);
		else m_inactive.remove(pp);
		ClearPageLRU(pp);
	}
	m_free_list.push_back(pp);
	set_bit(PG_free, pp->flags);
	unlock();
//	if (PageLocked(pp)) pp->unlock();
	F_LOG_DEBUG(L"page", L" page=%p, ref=%d, flag=%X, delete page", pp, pp->_refcount, pp->flags);
}

void CPageManager::cache_add(page* pp)
{
	F_LOG_DEBUG(L"page", L" page=%p, ref=%d, add to cache/inactive. flag=0X%x, inactive=%d, active=%d", pp, pp->_refcount, pp->flags, m_inactive.size(), m_active.size());
	lock();
	m_inactive.push_back(pp);
	unlock();
	SetPageLRU(pp);
}

void CPageManager::cache_activate_page(page* pp)
{
	F_LOG_DEBUG(L"page", L" page=%p, ref=%d, cache to active. flag=0X%x, inactive=%d, active=%d", pp, pp->_refcount, pp->flags, m_inactive.size(), m_active.size());

	// 源代码中，对比cache中的每个page，是否为当前page，是的话，就设置active
	lock();
	m_inactive.remove(pp);
	m_active.push_back(pp);
	unlock();
	SetPageActive(pp);
	SetPageLRU(pp);
}

void CPageManager::activate_page(page* pp)
{
	F_LOG_DEBUG(L"page", L" page=%p, ref=%d, inactive to active. flag=0X%x, inactive=%d, active=%d", pp, pp->_refcount, pp->flags, m_inactive.size(), m_active.size());
	lock();
	m_inactive.remove(pp);
	m_active.push_back(pp);
	unlock();
	SetPageActive(pp);
}

void CPageManager::del_page_from_lru(page* pp)
{
	F_LOG_DEBUG(L"page", L" page=%p, ref=%d, del from lru. flag=0X%x, inactive=%d, active=%d", pp, pp->_refcount, pp->flags, m_inactive.size(), m_active.size());
	lock();
	if (PageActive(pp)) m_active.remove(pp);
	else m_inactive.remove(pp);
//	m_free_list.push_back(pp);
	unlock();
}

void page::del(void)
{
	m_manager->DeletePage(this);
}


void lru_cache_add(page* pp)
{
	pp->m_manager->cache_add(pp);
}

/* Mark a page as having seen activity.
 *
 * inactive,unreferenced	->	inactive,referenced
 * inactive,referenced		->	active,unreferenced
 * active,unreferenced		->	active,referenced
 *
 * When a newly allocated page is not yet visible, so safe for non-atomic ops, __SetPageReferenced(page) may be substituted for mark_page_accessed(page). */
 // Linux的LRU管理，将page标记成active。当pg_referenced=0时，将pg_referenced置1。为1是，移入active列表
 // https://www.cnblogs.com/muahao/p/10109712.html
void mark_page_accessed(page* pp)
{
	//	pp = compound_head(pp);
	if (!PageReferenced(pp))
	{
		SetPageReferenced(pp);
	}
	else if (PageUnevictable(pp))
	{
		/* Unevictable pages are on the "LRU_UNEVICTABLE" list. But, this list is never rotated or maintained, so
		   marking an evictable page accessed has no effect.	 */
	}
	else if (!PageActive(pp))
	{
		/* If the page is on the LRU, queue it for activation via lru_pvecs.activate_page. Otherwise, assume the page
		   is on a pagevec, mark it active and it'll be moved to the active LRU on the next drain.	 */
		if (PageLRU(pp))		pp->m_manager->activate_page(pp);
		else					pp->m_manager->cache_activate_page(pp);
		ClearPageReferenced(pp);
#if 0 //<TODO>
		workingset_activation(pp);	//age管理
#endif
	}
	if (page_is_idle(pp))		clear_page_idle(pp);
}

void del_page_from_lru_list(page* pp, lruvec* lru)
{
	pp->m_manager->del_page_from_lru(pp);
}



/* Mark a page as having seen activity.
 *
 * inactive,unreferenced	->	inactive,referenced
 * inactive,referenced		->	active,unreferenced
 * active,unreferenced		->	active,referenced
 *
 * When a newly allocated page is not yet visible, so safe for non-atomic ops, __SetPageReferenced(page) may be substituted for mark_page_accessed(page). */
// Linux的LRU管理，将page标记成active，根据需要转入active列表。
// https://www.cnblogs.com/muahao/p/10109712.html
//void mark_page_accessed(struct page* pp)
//{
////	pp = compound_head(pp);
//#if 1 //TODO
//	if (!PageReferenced(pp))
//	{
//		SetPageReferenced(pp);
//	}
//	else if (PageUnevictable(pp))
//	{
//		/* Unevictable pages are on the "LRU_UNEVICTABLE" list. But, this list is never rotated or maintained, so
//		   marking an evictable page accessed has no effect.	 */
//	}
//	else if (!PageActive(pp))
//	{
//		/* If the page is on the LRU, queue it for activation via lru_pvecs.activate_page. Otherwise, assume the page
//		   is on a pagevec, mark it active and it'll be moved to the active LRU on the next drain.	 */
//		if (PageLRU(pp))		activate_page(pp);
//		else					__lru_cache_activate_page(pp);
//		ClearPageReferenced(pp);
//		workingset_activation(pp);
//	}
//#else
//	JCASSERT(0);
//#endif	
//	if (page_is_idle(pp))		clear_page_idle(pp);
//}
//EXPORT_SYMBOL(mark_page_accessed);

#if 0
//<YUAN> ref: mm/filemap.c/pagecache_get_page()
/* pagecache_get_page - Find and get a reference to a page.
 * @mapping: The address_space to search.
 * @index: The page index.
 * @fgp_flags: %FGP flags modify how the page is returned.
 * @gfp_mask: Memory allocation flags to use if %FGP_CREAT is specified.
 *
 * Looks up the page cache entry at @mapping & @index.
 *
 * @fgp_flags can be zero or more of these flags:
 *
 * * %FGP_ACCESSED - The page will be marked accessed.
 * * %FGP_LOCK - The page is returned locked.
 * * %FGP_HEAD - If the page is present and a THP, return the head page rather than the exact page specified by the index.
 * * %FGP_ENTRY - If there is a shadow / swap / DAX entry, return it instead of allocating a new page to replace it.
 * * %FGP_CREAT - If no page is present then a new page is allocated using
 *   @gfp_mask and added to the page cache and the VM's LRU list. The page is returned locked and with an increased refcount.
 * * %FGP_FOR_MMAP - The caller wants to do its own locking dance if the page is already in cache.  If the page was allocated,
     unlock it before returning so the caller can do the same dance.
 * * %FGP_WRITE - The page will be written
 * * %FGP_NOFS - __GFP_FS will get cleared in gfp mask
 * * %FGP_NOWAIT - Don't get blocked by page lock
 *
 * If %FGP_LOCK or %FGP_CREAT are specified then the function may sleep even
 * if the %GFP flags specified for %FGP_CREAT are atomic.
 *
 * If there is a page cache page, it is returned with an increased refcount.
 *
 * Return: The found page or %NULL otherwise. */
page* pagecache_get_page(address_space* mapping, pgoff_t index, int fgp_flags, gfp_t gfp_mask)
{
	LOG_DEBUG(L"flag = %s", FGP2String(fgp_flags).c_str());
	page* pp = NULL;
	auto it = mapping->i_pages.find(index);
	if (it != mapping->i_pages.end())
	{
		pp = it->second;
		if (pp->mapping != mapping) THROW_ERROR(ERR_APP, L"page mapping does not match");
		atomic_inc(&pp->_refcount);
		if (fgp_flags & FGP_LOCK)
		{
			lock_page(pp);
		}
		if (fgp_flags & FGP_ACCESSED) { mark_page_accessed(pp); }
		else if (fgp_flags & FGP_WRITE) 
		{		/* Clear idle flag for buffer write */
			if (page_is_idle(pp))			clear_page_idle(pp);
		}
		if (!(fgp_flags & FGP_HEAD)) 
		{
#if 0 //TODO
			page = find_subpage(page, index); 
#else
			JCASSERT(0);
#endif
		}

	}
	else
	{	// no_page
		if (fgp_flags & FGP_CREAT)
		{
			// allocate page
			pp = new page;
			if (!pp) THROW_ERROR(ERR_MEM, L"failed on allocating page");
			memset(pp, 0, sizeof(page));
			pp->virtual_add = new BYTE[PAGE_SIZE];
			// read page 
			// add_to_page_cache_lru(page, mapping, index, flag)
			// __add_to_page_locked(page, mapping, index, flag, shadowp)
			pp->mapping = mapping;
			pp->index = index;
			atomic_set(&pp->_refcount, 1);
			// add pp to mapping
			mapping->i_pages.insert(std::make_pair(index, pp));
		}
	}
	return pp;
}
#endif


page* grab_cache_page(address_space* mapping, pgoff_t index)
{
	return find_or_create_page(mapping, index, mapping_gfp_mask(mapping));
//	return pagecache_get_page(mapping, index, FGP_LOCK|FGP_ACCESSED|FGP_CREAT, 0);
}

//page* grab_cache_page_write_begin(address_space* mapping, pgoff_t index, int flag)
//{
//	page* pp = pagecache_get_page(mapping, index, FGP_LOCK|FGP_WRITE|FGP_CREAT, 0);
//	JCASSERT(0);
//	// wait_on_page_writeback()
//	return pp;
//}


//int PageUptodate(page* page_ptr)
//{
//	int ret = test_bit(PG_uptodate, &page_ptr->flags);
//	return ret;
//}

/* Drop a ref, return true if the refcount fell to zero (the page has no users) */
inline int page::put_page_testzero(void)
{
//	VM_BUG_ON_PAGE(page_ref_count(page) == 0, page);
	F_LOG_DEBUG(L"page", L" page=%p, ref=%d, type=%s, index=%d", this, _refcount, m_type.c_str(), index);
	JCASSERT(atomic_read(&_refcount) > 0);
//	return page_ref_dec_and_test(page);
	return atomic_dec_and_test(&_refcount)==0;
}



page* page::put_page(void)
{
//	page = compound_head(page);
	/* For devmap managed pages we need to catch refcount transition from 2 to 1, when refcount reach one it means the page is free and we need to inform the device driver through callback. See include/linux/memremap.h and HMM for details. */
	//if (page_is_devmap_managed(page))
	//{
	//	put_devmap_managed_page(page);
	//	return;
	//}
	F_LOG_DEBUG(L"page", L" page=%p, ref=%d, dirty=%d, flag=%X", this, _refcount, PageDirty(this), flags);
	// 当count减到0时，返回true
	if (put_page_testzero())
	{
//		__put_page();
		m_manager->DeletePage(this);
		return nullptr;
	}
	return this;
}

/* find_get_page - find and get a page reference
 * @mapping: the address_space to search
 * @offset: the page index
 *
 * Looks up the page cache slot at @mapping & @offset.  If there is a page cache page, it is returned with an
 increased refcount. Otherwise, %NULL is returned. */
page* find_get_page(address_space* mapping, pgoff_t index)
{
	return pagecache_get_page(mapping, index, 0, 0);
}



/* Mark the page dirty, and set it dirty in the page cache, and mark the inode dirty.
 * If warn is true, then emit a warning if the page is not uptodate and has not been truncated.
 * The caller must hold lock_page_memcg(). */
void __set_page_dirty(page* page, address_space* mapping, int warn)
{
	unsigned long flags=0;
	xa_lock_irqsave(&(mapping->i_pages), flags);
	if (page->mapping)
	{	/* Race with truncate? */
		JCASSERT(!(warn && !PageUptodate(page)));
		account_page_dirtied(page, mapping);
		__xa_set_mark(&mapping->i_pages, page_index(page), PAGECACHE_TAG_DIRTY);
	}
	xa_unlock_irqrestore(&(mapping->i_pages), flags);
}


/* For address_spaces which do not use buffers.  Just tag the page as dirty in the xarray.
 *
 * This is also used when a single buffer is being dirtied: we want to set the page dirty in that case, but not all the 
 buffers.  This is a "bottom-up" dirtying, whereas __set_page_dirty_buffers() is a "top-down" dirtying.
 *
 * The caller must ensure this doesn't race with truncation.  Most will simply hold the page lock, but e.g. zap_pte_range() 
 calls with the page mapped and the pte lock held, which also locks out truncation. */
int __set_page_dirty_nobuffers(struct page* page)
{
	lock_page_memcg(page);
	if (!TestSetPageDirty(page))
	{
		address_space* mapping = page->mapping;// page_mapping(page);

		if (!mapping)
		{
			unlock_page_memcg(page);
			return 1;
		}
		__set_page_dirty(page, mapping, !PagePrivate(page));
		unlock_page_memcg(page);

		if (mapping->host)
		{
			/* !PageAnon && !swapper_space */
			__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
		}
		return 1;
	}
	unlock_page_memcg(page);
	return 0;
}

//<YUAN> mm/page-writeback.c
/* Clear a page's dirty flag, while caring for dirty memory accounting. Returns true if the page was previously dirty.
 *
 * This is for preparing to put the page under writeout.  We leave the page tagged as dirty in the xarray so that a concurrent write-for-sync can discover it via a PAGECACHE_TAG_DIRTY walk.  The ->writepage implementation will run either set_page_writeback() or set_page_dirty(), at which stage we bring the page's dirty flag and xarray dirty tag back into sync.
 *
 * This incoherency between the page's dirty flag and xarray tag is unfortunate, but it only exists while the page is locked. */
int clear_page_dirty_for_io(page* ppage)
{
/*	<YUAN> 调用这个函数用于强制回写脏页，调用的主要过程如下：
	->clear_page_dirty_for_io(page) //对于回写的每一个页
		->page_mkclean(page) //清脏标记  mm/rmap.c 
			->page_mkclean_one //反向映射查找这个页的每个vma，调用清脏标记和写保护处理
				->entry = pte_wrprotect(entry);     //写保护处理，设置只读
				entry = pte_mkclean(entry); //清脏标记 set_pte_at(vma->vm_mm, address, pte, entry) //设置到页表项中

	->TestClearPageDirty(page) //清页描述符脏标记 */
	address_space* mapping = page_mapping(ppage);
	int ret = 0;

//	VM_BUG_ON_PAGE(!PageLocked(ppage), ppage);
	JCASSERT(PageLocked(ppage));

	if (mapping && mapping_can_writeback(mapping))
	{
		struct inode* inode = mapping->host;
		bdi_writeback* wb;
		wb_lock_cookie cookie = {};

		/* Yes, Virginia, this is indeed insane.
		 *
		 * We use this sequence to make sure that
		 *  (a) we account for dirty stats properly
		 *  (b) we tell the low-level filesystem to mark the whole page dirty if it was dirty in a pagetable. Only to then
		 *  (c) clean the page again and return 1 to cause the writeback.
		 * This way we avoid all nasty races with the dirty bit in multiple places and clearing them concurrently from different threads.
		 * Note! Normally the "set_page_dirty(page)" has no effect on the actual dirty bit - since that will already usually be set. But we need the side effects, and it can help us avoid races.
		 * We basically use the page "master dirty bit" as a serialization point for all the different threads doing their things.*/
		if (page_mkclean(ppage)) 	set_page_dirty(ppage);
		/* We carefully synchronise fault handlers against installing a dirty pte and marking the page dirty at this point.  We do this by having them hold the page lock while dirtying the page, and pages are always locked coming in here, so we get the desired exclusion.	 */
		wb = unlocked_inode_to_wb_begin(inode, &cookie);
		if (TestClearPageDirty(ppage))
		{
			dec_lruvec_page_state(ppage, NR_FILE_DIRTY);
//			dec_zone_page_state(ppage, NR_ZONE_WRITE_PENDING);
			dec_wb_stat(wb, WB_RECLAIMABLE);
			ret = 1;
		}
		unlocked_inode_to_wb_end(inode, &cookie);
		return ret;
	}
	return TestClearPageDirty(ppage);
}
//EXPORT_SYMBOL(clear_page_dirty_for_io);

int __test_set_page_writeback(page* ppage, bool keep_write)
{
	address_space* mapping = page_mapping(ppage);
	int ret/*, access_ret*/;

	lock_page_memcg(ppage);
	if (mapping && mapping_use_writeback_tags(mapping))
	{
		XA_STATE(xas, &mapping->i_pages, page_index(ppage));
		struct inode* inode = mapping->host;
		struct backing_dev_info* bdi = inode_to_bdi(inode);
		unsigned long flags=0;

		xas_lock_irqsave(&xas, flags);
		xas_load(&xas);
		ret = TestSetPageWriteback(ppage);
		if (!ret)
		{
			bool on_wblist;
			on_wblist = mapping->mapping_tagged(PAGECACHE_TAG_WRITEBACK);
			xas_set_mark(&xas, PAGECACHE_TAG_WRITEBACK);
#if 1 //忽略 bdi
			if (bdi->capabilities & BDI_CAP_WRITEBACK_ACCT)		inc_wb_stat(inode_to_wb(inode), WB_WRITEBACK);
#endif
			/* We can come through here when swapping anonymous pages, so we don't necessarily have an inode to track
			 * for sync. */
			if (mapping->host && !on_wblist)	sb_mark_inode_writeback(mapping->host);
		}
		if (!PageDirty(ppage))	xas_clear_mark(&xas, PAGECACHE_TAG_DIRTY);
		if (!keep_write)		xas_clear_mark(&xas, PAGECACHE_TAG_TOWRITE);
		xas_unlock_irqrestore(&xas, flags);
	}
	else
	{
		ret = TestSetPageWriteback(ppage);
	}
	if (!ret)
	{
		//inc_lruvec_page_state(ppage, NR_WRITEBACK);
		//inc_zone_page_state(ppage, NR_ZONE_WRITE_PENDING);
	}
	unlock_page_memcg(ppage);
//	access_ret = arch_make_page_accessible(ppage);
	/* If writeback has been triggered on a page that cannot be made accessible, it is too late to recover here. */
//	VM_BUG_ON_PAGE(access_ret != 0, ppage);
//	JCASSERT(access_ret == 0);

	return ret;
}
//EXPORT_SYMBOL(__test_set_page_writeback);

int set_page_writeback(page* pp)
{
	return __test_set_page_writeback(pp, false);
}

static page* find_get_entry(xa_state* xas, pgoff_t max, xa_mark_t mark)
{
	page* ppage;

retry:
	if (mark == XA_PRESENT) 	ppage = xas_find(xas, max);
	else						ppage = xas_find_marked(xas, max, mark);

	if (xas_retry(xas, ppage)) 			goto retry;
	/* A shadow entry of a recently evicted page, a swap entry from shmem/tmpfs or a DAX entry.  Return it without
	   attempting to raise page count.	 */
	if (!ppage || xa_is_value(ppage))	return ppage;

#if 0 //TODO
	if (!page_cache_get_speculative(ppage))	goto reset;
#endif
	/* Has the page moved or been split? */
	if (unlikely(ppage != xas_reload(xas)))
	{
		ppage->put_page();
		goto reset;
	}

	return ppage;
reset:
	xas_reset(xas);
	goto retry;
}

#if 0
/**
 * find_get_pages_range_tag - Find and return head pages matching @tag.
 * @mapping:	the address_space to search
 * @index:		the starting page index
 * @end:		The final page index (inclusive)
 * @tag:		the tag index
 * @nr_pages:	the maximum number of pages
 * @pages:	where the resulting pages are placed
 *
 * Like find_get_pages(), except we only return head pages which are tagged with @tag.  @index is updated to the
   index immediately after the last page we return, ready for the next iteration.
 * Return: the number of pages which were found.
 */
unsigned find_get_pages_range_tag(address_space* mapping, pgoff_t* index, pgoff_t end, xa_mark_t tag, 
	unsigned int nr_pages, page** pages)
{
	LOG_DEBUG(L"find pages with tag: %s", PageTag2String(tag).c_str());
	XA_STATE(xas, &mapping->i_pages, *index);
	//page* ppage;
	unsigned ret = 0;

	if (unlikely(!nr_pages))	return 0;
	auto it = mapping->i_pages.begin();
	for (; it != mapping->i_pages.end() && it->first <= end; ++it)
	{
		if (it->first < *index) continue;
		page* pp = it->second;
		/* Shadow entries should never be tagged, but this iteration is lockless so there is a window for page reclaim 
		   to evict a page we saw tagged.  Skip over it. */
		if (xa_is_value(pp)) continue;
		if (pp->is_marked(tag))
		{
			pages[ret] = pp;
			if (++ret == nr_pages)
			{
				*index = pp->index;
				return ret;
			}
		}
	}

#if 0 //REF
//	rcu_read_lock();
	while ((ppage = find_get_entry(&xas, end, tag)))
	{
		/* Shadow entries should never be tagged, but this iteration is lockless so there is a window for page reclaim 
		   to evict a page we saw tagged.  Skip over it. */
		if (xa_is_value(ppage))		continue;	//判断返回的值是否为指针

		pages[ret] = ppage;
		if (++ret == nr_pages)
		{
//			*index = ppage->index + thp_nr_pages(ppage);
			*index = ppage->index;
			goto out;
		}
	}
#endif
	// 没有找到足够的page
	/* We come here when we got to @end. We take care to not overflow the index @index as it confuses some of the 
	   callers. This breaks the iteration when there is a page at index -1 but that is already broken anyway. */
	if (end == (pgoff_t)-1)		*index = (pgoff_t)-1;
	else		*index = end + 1;
//out:
//	rcu_read_unlock();
	return ret;
}
#endif

unsigned find_get_pages_range_tag(address_space* mapping, pgoff_t* index, pgoff_t end, xa_mark_t tag, unsigned int nr_pages, page** pages);


unsigned pagevec_lookup_range_tag(pagevec* pvec, address_space* mapping, pgoff_t* index, pgoff_t end, xa_mark_t tag)
{
	pvec->nr = find_get_pages_range_tag(mapping, index, end, tag, PAGEVEC_SIZE, pvec->pages);
	return pagevec_count(pvec);
}

unsigned pagevec_lookup_tag(pagevec* pvec, address_space* mapping, pgoff_t* index, xa_mark_t tag)
{
	return pagevec_lookup_range_tag(pvec, mapping, index, (pgoff_t)-1, tag);
}


/* The pages which we're about to release may be in the deferred lru-addition queues.  That would prevent them from really being freed right now.  That's OK from a correctness point of view but is inefficient - those pages may be cache-warm and we want to give them back to the page allocator ASAP.
 *
 * So __pagevec_release() will drain those queues here.  __pagevec_lru_add() and __pagevec_lru_add_active() call release_pages() directly to avoid mutual recursion. */
void __pagevec_release(pagevec* pvec)
{
	if (!pvec->percpu_pvec_drained)
	{
		//		lru_add_drain();
		pvec->percpu_pvec_drained = true;
	}
	release_pages(pvec->pages, pagevec_count(pvec));
	pagevec_reinit(pvec);
}

/* release_pages - batched put_page()
 * @pages: array of pages to release
 * @nr: number of pages
 *
 * Decrement the reference count on all the pages in @pages.  If it fell to zero, remove the page from the LRU and free it.*/
//inline void release_pages(page** pages, int nr)
//{
//	int i;
//	LIST_HEAD(pages_to_free);
//	struct lruvec* lruvec = NULL;
////	unsigned long flags;
////	unsigned int lock_batch;
//
//	for (i = 0; i < nr; i++)
//	{
//		page* ppage = pages[i];
//		/* Make sure the IRQ-safe lock-holding time does not get excessive with a continuous string of pages from the same lruvec. The lock is held only if lruvec != NULL. */
//#if 0
//		//if (lruvec && ++lock_batch == SWAP_CLUSTER_MAX)
//		//{
//		//	unlock_page_lruvec_irqrestore(lruvec, flags);
//		//	lruvec = NULL;
//		//}
//
//		//ppage = compound_head(ppage);
//		//if (is_huge_zero_page(ppage)) 		continue;
//
//		if (is_zone_device_page(ppage))
//		{
//			if (lruvec)
//			{
//				unlock_page_lruvec_irqrestore(lruvec, flags);
//				lruvec = NULL;
//			}
//			/* ZONE_DEVICE pages that return 'false' from page_is_devmap_managed() do not require special processing, and instead, expect a call to put_page_testzero().	 */
//			if (page_is_devmap_managed(ppage))
//			{
//				put_devmap_managed_page(ppage);
//				continue;
//			}
//			if (put_page_testzero(ppage))	put_dev_pagemap(ppage->pgmap);
//			continue;
//		}
//
//		if (!put_page_testzero(ppage))		continue;
//
//		//if (PageCompound(ppage)) 
//		//{
//		//	if (lruvec) 
//		//	{
//		//		unlock_page_lruvec_irqrestore(lruvec, flags);
//		//		lruvec = NULL;
//		//	}
//		//	__put_compound_page(ppage);
//		//	continue;
//		//}
//#endif
//
//		if (PageLRU(ppage))
//		{
//			//struct lruvec* prev_lruvec = lruvec;
//			//lruvec = relock_page_lruvec_irqsave(ppage, lruvec, &flags);
//			//if (prev_lruvec != lruvec)	lock_batch = 0;
//			del_page_from_lru_list(ppage, lruvec);
//			//			__clear_page_lru_flags(ppage);
//			ClearPageLRU(ppage);
//			ClearPageActive(ppage);
//			ClearPageUnevictable(ppage);
//		}
////		__ClearPageWaiters(ppage);
////		list_add(&ppage->lru, &pages_to_free);
//	}
//	//	if (lruvec)		unlock_page_lruvec_irqrestore(lruvec, flags);
////	mem_cgroup_uncharge_list(&pages_to_free);
////	free_unref_page_list(&pages_to_free);
//}
void release_pages(page** pages, int nr)
{
	LOG_STACK_TRACE();
	int i;
//	LIST_HEAD(pages_to_free);
	std::list<page*> pages_to_free;
	lruvec* lruvec = NULL;
	//unsigned long flags;
	//unsigned int lock_batch;

	for (i = 0; i < nr; i++) 
	{
		page* ppage = pages[i];
		if (!ppage) continue;

		/* Make sure the IRQ-safe lock-holding time does not get excessive with a continuous string of pages from the same lruvec. The lock is held only if lruvec != NULL. */
		//if (lruvec && ++lock_batch == SWAP_CLUSTER_MAX) 
		//{
		//	unlock_page_lruvec_irqrestore(lruvec, flags);
		//	lruvec = NULL;
		//}

		//ppage = compound_head(ppage);
		//if (is_huge_zero_page(ppage))		continue;

		//if (is_zone_device_page(ppage)) 
		//{
		//	if (lruvec)
		//	{
		//		unlock_page_lruvec_irqrestore(lruvec, flags);
		//		lruvec = NULL;
		//	}
		//	/* ZONE_DEVICE pages that return 'false' from page_is_devmap_managed() do not require special processing, and instead, expect a call to put_page_testzero(). */
		//	if (page_is_devmap_managed(ppage)) 
		//	{
		//		put_devmap_managed_page(ppage);
		//		continue;
		//	}
		//	if (put_page_testzero(ppage))
		//		put_dev_pagemap(ppage->pgmap);
		//	continue;
		//}

		if (!ppage->put_page_testzero())	continue;

		//if (PageCompound(ppage)) 
		//{
		//	if (lruvec) 
		//	{
		//		unlock_page_lruvec_irqrestore(lruvec, flags);
		//		lruvec = NULL;
		//	}
		//	__put_compound_page(ppage);
		//	continue;
		//}

		if (PageLRU(ppage)) 
		{
			//lruvec* prev_lruvec = lruvec;
			//lruvec = relock_page_lruvec_irqsave(ppage, lruvec, &flags);
			//if (prev_lruvec != lruvec) lock_batch = 0;

			del_page_from_lru_list(ppage, lruvec);
	//		__clear_page_lru_flags(ppage);
			JCASSERT(PageLRU(ppage));
			ClearPageLRU(ppage);
			if (PageActive(ppage) && PageUnevictable(ppage)) {}
			else
			{
				ClearPageActive(ppage);
				ClearPageUnevictable(ppage);
			}
		}
//		__ClearPageWaiters(ppage);
		ClearPageWaiters(ppage);
//		list_add(&ppage->lru, &pages_to_free);
		pages_to_free.push_back(ppage);
	}
	//if (lruvec)	unlock_page_lruvec_irqrestore(lruvec, flags);

//	mem_cgroup_uncharge_list(&pages_to_free);
//	free_unref_page_list(&pages_to_free);
	for (auto it = pages_to_free.begin(); it != pages_to_free.end(); ++it)
	{
		(*it)->del();
	}
}

/* Free a list of 0-order pages */
#if 0
void free_unref_page_list(list_head* list)
{
	LOG_STACK_TRACE()
	page* ppage, * next;
	unsigned long flags, pfn;
	int batch_count = 0;
#if 0

	/* Prepare pages for freeing */
	list_for_each_entry_safe(page, ppage, next, list, lru) 
	{
		pfn = page_to_pfn(ppage);
		if (!free_unref_page_prepare(ppage, pfn))	list_del(&ppage->lru);
		set_page_private(ppage, pfn);
	}

//	local_irq_save(flags);
	list_for_each_entry_safe(page, ppage, next, list, lru)
	{
		unsigned long pfn = page_private(ppage);

		set_page_private(ppage, 0);
//		trace_mm_page_free_batched(ppage);
		free_unref_page_commit(ppage, pfn);

		/* Guard against excessive IRQ disabled times when we get a large list of pages to free. */
		//if (++batch_count == SWAP_CLUSTER_MAX)
		//{
		//	local_irq_restore(flags);
		//	batch_count = 0;
		//	local_irq_save(flags);
		//}
	}
//	local_irq_restore(flags);
#else
	list_for_each_entry_safe(page, ppage, next, list, lru) 
	{
		//pfn = page_to_pfn(ppage);
		//if (!free_unref_page_prepare(ppage, pfn))	list_del(&ppage->lru);
		//set_page_private(ppage, pfn);
		ppage->del();
	}


#endif
}
#endif
/*
 * Return true if this page is mapped into pagetables.
 * For compound page it returns true if any subpage of compound page is mapped.
 */
//bool page_mapped(struct page* page)
bool page::page_mapped(void)
{
	return (atomic_read(&_mapcount) >= 0);
	//int i;

	//if (likely(!PageCompound(this)))	return atomic_read(&this->_mapcount) >= 0;
	//this = compound_head(this);
	//if (atomic_read(compound_mapcount_ptr(this)) >= 0)
	//	return true;
	//if (PageHuge(this))
	//	return false;
	//for (i = 0; i < compound_nr(this); i++) {
	//	if (atomic_read(&this[i]._mapcount) >= 0)
	//		return true;
	//}
	//return false;
}
//EXPORT_SYMBOL(page_mapped);

/*
 * This cancels just the dirty bit on the kernel page itself, it does NOT
 * actually remove dirty bits on any mmap's that may be around. It also
 * leaves the page tagged dirty, so any sync activity will still find it on
 * the dirty lists, and in particular, clear_page_dirty_for_io() will still
 * look at the dirty bits in the VM.
 *
 * Doing this should *normally* only ever be done when a page is truncated,
 * and is not actually mapped anywhere at all. However, fs/buffer.c does
 * this when it notices that somebody has cleaned out all the buffers on a
 * page without actually doing it through the VM. Can you say "ext3 is
 * horribly ugly"? Thought you could.
 */
//void __cancel_dirty_page(struct page* page)
void page::cancel_dirty_page(void)
{
	if (PageDirty(this))
	{
		address_space* mapping = page_mapping(this);

		if (mapping_can_writeback(mapping)) {
			struct inode* inode = mapping->host;
			bdi_writeback* wb;
			wb_lock_cookie cookie = {};

			lock_page_memcg(this);
			wb = unlocked_inode_to_wb_begin(inode, &cookie);
			if (TestClearPageDirty(this))
				account_page_cleaned(this, mapping, wb);
			unlocked_inode_to_wb_end(inode, &cookie);
			unlock_page_memcg(this);
		}
		else {
			ClearPageDirty(this);
		}
	}
}
//EXPORT_SYMBOL(__cancel_dirty_page);
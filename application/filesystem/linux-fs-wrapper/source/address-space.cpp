///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include "../include/address-space.h"
#include "../include/mm_types.h"
//#include "../include/fs.h"

LOCAL_LOGGER_ENABLE(L"vfs.address_space", LOGGER_LEVEL_DEBUGINFO);


/**
 * __xa_clear_mark() - Clear this mark on this entry while locked.
 * @xa: XArray.
 * @index: Index of entry.
 * @mark: Mark number.
 *
 * Context: Any context.  Expects xa_lock to be held on entry.
 */
void __xa_clear_mark(struct xarray* xa, unsigned long index, xa_mark_t mark)
{
	XA_STATE(xas, xa, index);
	void* entry = xas_load(&xas);
	if (entry)	xas_clear_mark(&xas, mark);
}
//EXPORT_SYMBOL(__xa_clear_mark);

void mapping_set_gfp_mask(address_space* m, gfp_t mask)
{
	m->gfp_mask = mask;
}

/* Dirty a page.
 * For pages with a mapping this should be done under the page lock for the benefit of asynchronous memory errors who
   prefer a consistent dirty state. This rule can be broken in some special cases, but should be better not to. */
int set_page_dirty(page* page)
{
	address_space* mapping = page->mapping;
	//	page = compound_head(page);
	if (likely(mapping))
	{	/* readahead/lru_deactivate_page could remain PG_readahead/PG_reclaim due to race with end_page_writeback. About readahead, if the page is written, the flags would be reset. So no problem. About lru_deactivate_page, if the page is redirty, the flag will be reset. So no problem. but if the page is used by readahead it will confuse readahead and make it restart the size rampup process. But it's a trivial problem. */
		if (PageReclaim(page))		ClearPageReclaim(page);
		//		return mapping->a_ops->set_page_dirty(page);
		mapping->set_node_page_dirty(page);
	}
	if (!PageDirty(page))
	{
		if (!TestSetPageDirty(page))		return 1;
	}
	return 0;
}


page::page(CPageManager * manager) : m_manager(manager), virtual_add(nullptr)
{
//	LOG_STACK_TRACE();
	mapping = nullptr;
	index = 0;
	private_data = 0;
	_refcount = 1;
	index = -1;
	InitializeCriticalSection(&m_lock);
	InitializeConditionVariable(&m_state_condition);
}

page::page(void) : m_manager(nullptr), virtual_add(nullptr)
{
	//	LOG_STACK_TRACE();
	mapping = nullptr;
	index = 0;
	private_data = 0;
	_refcount = 1;
	index = -1;

	InitializeCriticalSection(&m_lock);
	InitializeConditionVariable(&m_state_condition);
}

page::~page(void)
{
#ifdef DEBUG_PAGE
	LOG_TRACK(L"page", L"page destory, page=0x%llX, add=0x%llX, ref=%d, type=%d, index=%d",
		this, virtual_add, _refcount, m_type, index);
#endif
	DeleteCriticalSection(&m_lock);
}

void page::init(CPageManager* manager, void* vmem)
{
	m_manager = manager;
	virtual_add = vmem;
}

void page::reinit(void)
{	// 重新初始化 page
	mapping = nullptr;
	index = -1;
	private_data = 0;
	_refcount = 1;
	flags = 0;
	page_type = 0;
#ifdef DEBUG_PAGE
	m_type = UNKNOWN;
	m_inode = 0;
#endif
}

/**
 * block_invalidatepage - invalidate part or all of a buffer-backed page
 *
 * @page: the page which is affected
 * @offset: start of the range to invalidate
 * @length: length of the range to invalidate
 *
 * block_invalidatepage() is called when all or part of the page has become invalidated by a truncate operation.
 *
 * block_invalidatepage() does not have to release all buffers, but it must ensure that no dirty buffer is left outside @offset and that no I/O is underway against any of the blocks which are outside the truncation point.  Because the caller is about to free (and possibly reuse) those blocks on-disk.
 */
//void block_invalidatepage(struct page* page, unsigned int offset, unsigned int length)
void address_space::invalidate_page(page* ppage, unsigned int offset, unsigned int length)
{
//	struct buffer_head* head, * bh, * next;
	unsigned int curr_off = 0;
	unsigned int stop = length + offset;

	BUG_ON(!PageLocked(ppage));
	if (!ppage->page_has_buffers())
		goto out;

#if 0
	/* Check for overflow */
	BUG_ON(stop > PAGE_SIZE || stop < length);

	head = ppage->page_buffers();
	bh = head;
	do 
	{
		unsigned int next_off = curr_off + bh->b_size;
		next = bh->b_this_page;

		/* Are we still fully in range ? */
		if (next_off > stop)
			goto out;

		/* is this block fully invalidated? */
		if (offset <= curr_off)
			discard_buffer(bh);
		curr_off = next_off;
		bh = next;
	} while (bh != head);

	/* We release buffers only if the entire page is being invalidated. The get_block cached value has been unconditionally invalidated, so real IO is not possible anymore.	 */
	if (length == PAGE_SIZE)
		try_to_release_page(ppage, 0);
#else
	JCASSERT(0);
#endif

out:
	return;
}

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
	{	/* readahead/lru_deactivate_page could remain PG_readahead/PG_reclaim due to race with end_page_writeback
		* About readahead, if the page is written, the flags would be reset. So no problem. About lru_deactivate_page,
		if the page is redirty, the flag will be reset. So no problem. but if the page is used by readahead it will
		confuse readahead and make it restart the size rampup process. But it's a trivial problem. */
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


page::page(void) 
{
//	LOG_STACK_TRACE();
	memset(this, 0, sizeof(page));
	_refcount = 1;
	index = -1;
	virtual_add = VirtualAlloc(0, PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
	if (virtual_add == 0) THROW_WIN32_ERROR(L"failed on allocate page");
#ifdef _DEBUG
	LOG_DEBUG(L"page allocated, page=0x%llX, add=0x%llX", this, virtual_add);
	back_add = virtual_add;
#endif
}

page::~page(void)
{
#ifdef _DEBUG
	LOG_DEBUG(L"page destory, page=0x%llX, add=0x%llX, ref=%d, type=%s, index=%d", 
		this, virtual_add, _refcount, m_type.c_str(), index);
	if (back_add != virtual_add) LOG_ERROR(L"[err] page address changed, org=0x%llX, new=0x%llX", back_add, virtual_add);
//		THROW_ERROR(ERR_APP, L"page address changed, org=0x%llX, new=0x%llX", back_add, virtual_add);
#endif
	VirtualFree(virtual_add, PAGE_SIZE, MEM_RELEASE);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include <linux-fs-wrapper.h>
#include <list>

class CLRU
{
public:
	CLRU(void);
	~CLRU(void);


public:
	// TODO, Linux中为了减少lru的lock时间，先将page放入cache中，然后等到cache满了再放入active或者inactive队列中。
	// cache是为每个cpu分配的，添加cache的话，可以减少cpu之间的lock。
	//	目前都没有实现cache功能，直接俄放入队列中。
	void cache_add(page* p);
	void cache_activate_page(page* pp);

	void activate_page(page* pp);
	void del_page_from_lru(page* pp);

protected:
	void lock(void) {};
	void unlock(void) {};


protected:
	// page cache
	size_t m_cache_nr;
	page* m_cache[PAGEVEC_SIZE];
	// inactive list
	std::list<page*> m_inactive;
	std::list<page*> m_active;
};

CLRU g_lru;

CLRU::CLRU(void)
{
	memset(m_cache, 0, sizeof(page*) * PAGEVEC_SIZE);
	m_cache_nr = 0;
}

CLRU::~CLRU(void)
{
	for (size_t ii = 0; ii < PAGEVEC_SIZE; ++ii)
	{
		delete m_cache[ii];
	}
	for (auto it = m_inactive.begin(); it != m_inactive.end(); it++)
	{
		delete (* it);
	}
}

void CLRU::cache_add(page* pp)
{
	lock();
	m_inactive.push_back(pp);
	unlock();
	SetPageLRU(pp);
}

void CLRU::activate_page(page* pp)
{
	lock();
	m_inactive.remove(pp);
	m_active.push_back(pp);
	unlock();
	SetPageActive(pp);
//	SetPageLRU(pp);
}

void CLRU::del_page_from_lru(page* pp)
{
	lock();
	if (PageActive(pp)) m_active.remove(pp);
	else m_inactive.remove(pp);
	unlock();

}

void CLRU::cache_activate_page(page* pp)
{
	// 源代码中，对比cache中的每个page，是否为当前page，是的话，就设置active
	lock();
	m_active.push_back(pp);
	unlock();
	SetPageActive(pp);
	SetPageLRU(pp);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== wriper

void lru_cache_add(page* pp)
{
	g_lru.cache_add(pp);
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
		if (PageLRU(pp))		g_lru.activate_page(pp);
		else					g_lru.cache_activate_page(pp);
		ClearPageReferenced(pp);
#if 0 //<TODO>
		workingset_activation(pp);	//age管理
#endif
	}
	if (page_is_idle(pp))		clear_page_idle(pp);
}

void del_page_from_lru_list(page* pp, lruvec* lru)
{
	g_lru.del_page_from_lru(pp);
}

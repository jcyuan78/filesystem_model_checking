///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "linux_comm.h"
#include "mm_types.h"

class CPageManager
{
public:
	CPageManager(size_t cache_size);		// 预留cache大小，以page为单位，要求时16的倍数。物理内存以64K为单位申请。
	~CPageManager(void);
public:
	friend struct page;
public:
	page* NewPage(void);/* { return (new page(this)); }*/
	void DeletePage(page* p);
	void GetStatus(size_t& cache_nr, size_t& free, size_t& active, size_t& inactive) const
	{
		cache_nr = m_cache_nr;
		free = m_free_list.size();
		active = m_active.size();
		inactive = m_inactive.size();
	}

public:
// page LRU
	void cache_add(page* p);
	void cache_activate_page(page* pp);

	void activate_page(page* pp);
	void del_page_from_lru(page* pp);

protected:
	inline void lock(void) { EnterCriticalSection(&m_free_list_lock); };
	inline void unlock(void) { LeaveCriticalSection(&m_free_list_lock); };
	size_t reclaim_pages(void);

protected:
	CRITICAL_SECTION m_page_wait_lock;
	CRITICAL_SECTION m_free_list_lock;

// LRU管理

	// page cache
	size_t m_cache_nr;
	page* m_cache;
	void* m_buffer;		// 缓存地址
	// inactive list
	std::list<page*> m_inactive;
	std::list<page*> m_active;
	std::list<page*> m_free_list;

};
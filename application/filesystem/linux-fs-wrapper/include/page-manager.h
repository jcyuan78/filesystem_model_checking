///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "linux_comm.h"
#include "mm_types.h"

class CPageManager
{
public:
	CPageManager(size_t cache_size);		// Ԥ��cache��С����pageΪ��λ��Ҫ��ʱ16�ı����������ڴ���64KΪ��λ���롣
	~CPageManager(void);
public:
	friend struct page;
public:
	page* NewPage(void);/* { return (new page(this)); }*/
	void DeletePage(page* p);

public:
// page LRU
	void cache_add(page* p);
	void cache_activate_page(page* pp);

	void activate_page(page* pp);
	void del_page_from_lru(page* pp);

protected:
	void lock(void) { EnterCriticalSection(&m_free_list_lock); };
	void unlock(void) { LeaveCriticalSection(&m_free_list_lock); };

protected:
	CRITICAL_SECTION m_page_wait_lock;
	CRITICAL_SECTION m_free_list_lock;

// LRU����

	// page cache
	size_t m_cache_nr;
	page* m_cache;
	void* m_buffer;		// �����ַ
	// inactive list
	std::list<page*> m_inactive;
	std::list<page*> m_active;
	std::list<page*> m_free_list;

};
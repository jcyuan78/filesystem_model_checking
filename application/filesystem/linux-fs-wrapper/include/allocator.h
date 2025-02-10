///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

template <class T>
class Allocator
{
public:
	typedef T OBJ_TYPE;
	typedef T* OBJ_PTR;
public:
	Allocator(size_t allocate_size)
	{
		InitializeCriticalSection(&m_allocator_locker);
		m_increment = allocate_size;
		AllocateObjBuffer(m_increment);
	}
	~Allocator(void)
	{
#ifdef _DEBUG
		//LOG_WARNING(L"buf_size=%d, free=%d", m_obj_nr, m_free_list.size());
		if (m_obj_nr != m_free_list.size())
		{
			//LOG_ERROR(L"[err] memory leak:on page cache, total pages=%lld, free pages=%lld", m_obj_nr, m_free_list.size())
			//	for (auto it = m_active.begin(); it != m_active.end(); ++it)
			//	{
			//		page* pp = *it;
			//		LOG_DEBUG_(1, L"leak page: page=%p, type=%d, inode=%d, index=%d, ref=%d, flag=%X, active", pp, pp->m_type, pp->m_inode, pp->_refcount, pp->flags);
			//	}
			//for (auto it = m_inactive.begin(); it != m_inactive.end(); ++it)
			//{
			//	page* pp = *it;
			//	LOG_DEBUG_(1, L"leak page: page=%p, type=%d, inode=%d, index=%d, ref=%d, flag=%X, inactive", pp, pp->m_type, pp->m_inode, pp->_refcount, pp->flags);
			//}
		}
#endif
		for (auto it = m_obj_buffer.begin(); it != m_obj_buffer.end(); ++it)
		{
			delete[](*it);
		}
		DeleteCriticalSection(&m_allocator_locker);
	}
	T* alloc_obj(void)
	{
		if (m_free_list.empty()) {	AllocateObjBuffer(m_increment);	}

		T* obj = nullptr;
//		while (1)	// retry
//		{
			alloc_lock();
			obj = m_free_list.front();
			m_free_list.pop_front();
			alloc_unlock();
//			if (obj) break;
//		}

		// 重新初始化 page
//		obj->reinit();
		return obj;
	}
	void free_obj(void* obj)
	{
		alloc_lock();
		m_free_list.push_back((T*)obj);
		alloc_unlock();
	}
protected:
	inline void alloc_lock(void) { EnterCriticalSection(&m_allocator_locker); }
	inline void alloc_unlock(void) { LeaveCriticalSection(&m_allocator_locker); }
	void AllocateObjBuffer(size_t nr)
	{
		// 当page不够时，每次申请一批page
//		size_t mem_size = page_nr * PAGE_SIZE;
		T* obj = new T[nr];
		if (obj == nullptr) THROW_ERROR(ERR_MEM, L"no enough memory for object cache, current obj=%lld, allocate obj=%lld", m_obj_nr, nr);

		alloc_lock();
		m_obj_buffer.push_back(obj);

		m_obj_nr += nr;
		for (size_t ii = 0; ii < nr; ++ii)
		{
			//obj[T].init(this);
			m_free_list.push_back(obj + ii);
		}
		alloc_unlock();
	}

protected:
	CRITICAL_SECTION m_allocator_locker;

	std::list<T*> m_free_list;
	// 每次申请一批，放入buffer
	std::list<T*> m_obj_buffer;
	size_t m_obj_nr = 0;	// 对象总数
	size_t m_increment;		// 一次申请buffer的增量
};

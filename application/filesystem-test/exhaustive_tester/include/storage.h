///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "f2fs_segment.h"
#include "blocks.h"
#include <list>

struct StorageDataBase
{
	void AddRef(void) { InterlockedIncrement(&m_ref); }

	BLOCK_DATA data;
	StorageDataBase* pre_ver=nullptr;		// 相同数据的前一个版本
	StorageDataBase* pre_write=nullptr;		// 前一笔write command
	long m_ref=1;
	// for debug
	LBLK_T lba;		// 对应的lba. (-1)表示其他数据。

protected:
	void Release(void);
};

class StorageDataManager
{
public:
	StorageDataManager(void);
	~StorageDataManager(void);

public:
	StorageDataBase* get(void);
	void put(StorageDataBase* sdata);

protected:
	void AllocateBuffer(size_t nr);
	void Reclaim(StorageDataBase* sdata);
	void Clear(void);
protected:
	std::list<StorageDataBase*> m_buffers;
	size_t m_free_nr;
	StorageDataBase* m_free_list;
	CRITICAL_SECTION m_lock;
};

class CStorage
{
public:
	CStorage(CF2fsSimulator * fs);
	~CStorage();

	void Initialize(void);
	void CopyFrom(const CStorage* src);
public:
	void BlockWrite(LBLK_T lba, CPageInfo * page);
	void BlockRead(LBLK_T lba, CPageInfo * page);
	void Sync(void);
	LBLK_T GetCacheNum(void);
	UINT GetMediaWriteNum(void) const { return m_cache_nr; }
	void Rollback(LBLK_T nr);
	void Reset(void);

	void DumpStorage(FILE* out);

protected:
	StorageDataBase *m_data[TOTAL_BLOCK_NR];
	CPageAllocator* m_pages;
	// 用cache模拟断电过程。cache 是一个循环队列，所有写入都会首先被写入cache中。当cache满时，队尾的内容会被写入磁盘。
	// 或者在sync()函数中，把所有内容写入磁盘。
	// 模拟power outage时，通过rollback()函数删除部分cache内容。
	// 

	StorageDataBase* m_cache_tail;
	UINT m_cache_nr;		// 累计的cache数量

	// for DEBUG
	UINT m_begin_cache_nr;
	StorageDataBase* m_begin_cache;
	const CStorage* m_parent;

	StorageDataManager* m_data_manager;
};


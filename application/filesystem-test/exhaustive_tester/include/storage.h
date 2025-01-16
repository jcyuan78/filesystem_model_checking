///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "f2fs_segment.h"
#include "blocks.h"

struct StorageEntry
{
public:
	LBLK_T lba;
	BLOCK_DATA data;
	// 仅用于storage。在power outage测试专用，标志数据的更新链。数据在cache中可以被多次更新。
	// 对于每个数据块，构成一个双向链表。prev指向数据的前一个版本，next指向数据的新版本。storage中的block时链表头。
	LBLK_T cache_next, cache_prev;
};

class CStorage
{
public:
	CStorage(CF2fsSimulator * fs);
	~CStorage() {};

	void Initialize(void);
	void CopyFrom(const CStorage* src);
public:
	void BlockWrite(LBLK_T lba, CPageInfo * page);
	void BlockRead(LBLK_T lba, CPageInfo * page);
	void Sync(void);
	LBLK_T GetCacheNum(void);
	void Rollback(LBLK_T nr);
	void Reset(void);

protected:
	void cache_enque(LBLK_T lba, LBLK_T cache_index);						// 节点插入cache
	void cache_deque(LBLK_T cache_index);		// cache index的节点移出

protected:
	StorageEntry m_data[TOTAL_BLOCK_NR];
	CPageAllocator* m_pages;
	// 用cache模拟断电过程。cache 是一个循环队列，所有写入都会首先被写入cache中。当cache满时，队尾的内容会被写入磁盘。
	// 或者在sync()函数中，把所有内容写入磁盘。
	// 模拟power outage时，通过rollback()函数删除部分cache内容。
	// 
	StorageEntry m_cache[SSD_CACHE_SIZE];
	LBLK_T m_cache_head, m_cache_tail, m_cache_size;
};
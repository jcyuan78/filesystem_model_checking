///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "f2fs_segment.h"
#include "blocks.h"

struct StorageEntry
{
public:
	UINT lba;
	BLOCK_DATA data;
	// ������storage����power outage����ר�ã���־���ݵĸ�������������cache�п��Ա���θ��¡�
	// ����ÿ�����ݿ飬����һ��˫������prevָ�����ݵ�ǰһ���汾��nextָ�����ݵ��°汾��storage�е�blockʱ����ͷ��
	WORD cache_next, cache_prev;
};

class CStorage
{
public:
	CStorage(CF2fsSimulator * fs);
	~CStorage() {};

	void Initialize(void);
	void CopyFrom(const CStorage* src);
public:
	void BlockWrite(UINT lba, CPageInfo * page);
	void BlockRead(UINT lba, CPageInfo * page);
	void Sync(void);
	UINT GetCacheNum(void);
	void Rollback(UINT nr);
	void Reset(void);

protected:
	void cache_enque(UINT lba, UINT cache_index);						// �ڵ����cache
	void cache_deque(UINT cache_index);		// cache index�Ľڵ��Ƴ�

protected:
	StorageEntry m_data[TOTAL_BLOCK_NR];
	CPageAllocator* m_pages;
	// ��cacheģ��ϵ���̡�cache ��һ��ѭ�����У�����д�붼�����ȱ�д��cache�С���cache��ʱ����β�����ݻᱻд����̡�
	// ������sync()�����У�����������д����̡�
	// ģ��power outageʱ��ͨ��rollback()����ɾ������cache���ݡ�
	// 
	StorageEntry m_cache[SSD_CACHE_SIZE];
	UINT m_cache_head, m_cache_tail, m_cache_size;
};
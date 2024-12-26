///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// pages.h: 定义page和block data buffer。这两个模拟Linux的缓存管理
#pragma once

#include "config.h"
#include "fs_simulator.h"
#include "blocks.h"

//#define PAGE_NEXT_FREE	data_index

//struct BLOCK_DATA;

class CPageInfo
{
public:
	void Init();

public:
	PHY_BLK phy_blk = INVALID_BLK;	// page所在物理位置
	// 标记page的温度，当page被写入SSD时更新。这个温度不是实际分配到温度，所有算法下都相同。仅用于统计。
	BLK_TEMP ttemp;
	// nid和offset在一起表示page的逻辑地址（在父节点中的位置）。主要用于GC时更新父节点的内容。这两个参数在调用 WriteBlockToSeg() 前设置
	// 当page指向node block（inode, index）时，其父节点为NAT。 nid表示node id，offset =INVALID_BLK表示指向node
	// 当page指向data block（data, dentry)等。其父节点为inode/index，nid表示父节点的nid，offset表示在父节点中的位置。

	NID	nid;						// 这个page所在的node的nid。对于node block，不需要更新父节点，更新NAT即可。nid=INVALID_BLK时，表示page无效。
	LBLK_T offset = INVALID_BLK;	// 如果page是一个node，offset=INVALID_BLK
	bool dirty = false;
public:
	// 用于性能统计
	UINT host_write = 0;
	friend class CPageAllocator;
protected:
	// 数据(对于inode 或者 direct node)
	union {
		PAGE_INDEX free_link;	// 指向 下一个free page
		BLOCK_DATA data;
	};
};

typedef CPageInfo* PPAGE;

class CPageAllocator
{
public:
//	typedef UINT INDEX;
	CPageAllocator(CF2fsSimulator* fs);
	~CPageAllocator(void);

	void CopyFrom(const CPageAllocator* src);
	void Reset(void);

public:
	void Init(size_t page_nr);
	// 申请一个page，data:true，同时申请数据，false，不要数据
	CPageInfo* allocate(bool data);
//	void free(PAGE_INDEX index);
	void free(CPageInfo* page);

	BLOCK_DATA * get_data(CPageInfo* page);

	inline CPageInfo* page(PAGE_INDEX index) {
		return (index >= m_page_nr) ? nullptr : (m_pages + index);
	}
	inline PAGE_INDEX page_id(CPageInfo* page) const { return (PAGE_INDEX)(page - m_pages); }

// 获取相关信息
public:
	UINT total_page_nr(void) { return MAX_PAGE_NUM; }
//	UINT total_data_nr(void) { return BLOCK_BUF_SIZE; }
	UINT free_page_nr(void) { return MAX_PAGE_NUM - m_used_nr; }
//	UINT free_data_nr(void) { return BLOCK_BUF_SIZE - m_buffer.m_used_nr; }

protected:
	CPageInfo m_pages[MAX_PAGE_NUM];
	PAGE_INDEX m_free_ptr, m_used_nr;
	UINT m_page_nr;
protected:
	PAGE_INDEX allocate_index(void);
//	CBufferManager m_buffer;
};
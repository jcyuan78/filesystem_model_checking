///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/fs_simulator.h"

#include "../include/f2fs_segment.h"

CPageAllocator::CPageAllocator(void)
	: m_page_nr(MAX_PAGE_NUM)
{

}

CPageAllocator::~CPageAllocator(void)
{

}

void CPageAllocator::Init(size_t page_nr)
{
	// 构建free链表
	for (UINT ii = 0; ii < m_page_nr; ++ii)
	{
//		m_free_pages[ii] = ii;
		m_pages[ii].PAGE_NEXT_FREE = ii + 1;
	}
	m_pages[m_page_nr - 1].PAGE_NEXT_FREE = INVALID_BLK;
	m_free_ptr = 0;
	m_used_nr = 0;
}

CPageAllocator::INDEX CPageAllocator::get_page(void)
{
	if (m_used_nr >= m_page_nr) THROW_ERROR(ERR_APP, L"out of memory for page buffer");
//	INDEX page = m_free_pages[m_head_free];
	INDEX page = m_free_ptr;
	m_free_ptr = m_pages[page].PAGE_NEXT_FREE;
	m_used_nr++;

	m_pages[page].Init();
//	m_head_free++;
	return page;
}

void CPageAllocator::put_page(CPageAllocator::INDEX page)
{
//	if (m_head_free == 0)	THROW_ERROR(ERR_APP, L"page buffer is full.");
	m_pages[page].PAGE_NEXT_FREE = m_free_ptr;
	m_free_ptr = page;
	m_used_nr--;

//	m_head_free--;
//	m_free_pages[m_head_free] = page;
}

void CPageInfo::Init()
{
	memset(this, 0xFF, sizeof(CPageInfo));
	phy_blk = INVALID_BLK;	// page所在物理位置
	// 标记page的温度，当page被写入SSD时更新。这个温度不是实际分配到温度，所有算法下都相同。仅用于统计。
	ttemp = BT_TEMP_NR;
	//在文件中的位置
	inode = INVALID_BLK;
	offset = INVALID_BLK;
	// 数据(对于inode 或者 direct node)
	data_index = INVALID_BLK;
	dirty = false;
	type = PAGE_DATA;
	host_write = 0;
	media_write = 0;
}

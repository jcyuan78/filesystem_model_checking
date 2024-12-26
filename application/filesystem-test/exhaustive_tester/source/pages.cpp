///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/fs_simulator.h"

#include "../include/f2fs_segment.h"
#include "../include/f2fs_simulator.h"

#include "../include/blocks.h"

CPageAllocator::CPageAllocator(CF2fsSimulator* fs)
	: m_page_nr(MAX_PAGE_NUM)/*, m_buffer(&fs->m_block_buf)*/
{

}

CPageAllocator::~CPageAllocator(void)
{

}

void CPageAllocator::CopyFrom(const CPageAllocator* src)
{

}

void CPageAllocator::Reset(void)
{
	Init(0);
}

void CPageAllocator::Init(size_t page_nr)
{
	// 构建free链表
	for (UINT ii = 0; ii < m_page_nr; ++ii)
	{
		m_pages[ii].free_link = ii + 1;
	}
	m_pages[m_page_nr - 1].free_link = INVALID_BLK;
	m_free_ptr = 0;
	m_used_nr = 0;
}

PAGE_INDEX CPageAllocator::allocate_index(void)
{
	if (m_used_nr >= m_page_nr) THROW_ERROR(ERR_APP, L"out of memory for page buffer");
	PAGE_INDEX page = m_free_ptr;
	m_free_ptr = m_pages[page].free_link;
	m_used_nr++;
	m_pages[page].Init();
	return page;
}

CPageInfo* CPageAllocator::allocate(bool data)
{
	PAGE_INDEX pid = allocate_index();
	CPageInfo* page = &m_pages[pid];
	return page;
}

//void CPageAllocator::free(PAGE_INDEX page)
void CPageAllocator::free(CPageInfo* page)
{
	if (page == nullptr) THROW_ERROR(ERR_USER, L"page %d has been released.", page);

	PAGE_INDEX page_id = this->page_id(page);
	m_pages[page_id].free_link = m_free_ptr;
	m_free_ptr = page_id;
	if (m_used_nr == 0) THROW_ERROR(ERR_APP, L"over free page");
	m_used_nr--;
}

BLOCK_DATA* CPageAllocator::get_data(CPageInfo* page)
{
	return &(page->data);
}

void CPageInfo::Init()
{
	memset(this, 0xFF, sizeof(CPageInfo));
	phy_blk = INVALID_BLK;	// page所在物理位置
	// 标记page的温度，当page被写入SSD时更新。这个温度不是实际分配到温度，所有算法下都相同。仅用于统计。
	ttemp = BT_TEMP_NR;
	//在文件中的位置
	nid = INVALID_BLK;
	offset = INVALID_BLK;
	// 数据(对于inode 或者 direct node)
	dirty = false;
	host_write = 0;
}

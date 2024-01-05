///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/pages.h"

CPageManager::CPageManager(void)
	: m_free_pages(nullptr), m_page_nr(0), m_head_free(0)
{

}

CPageManager::~CPageManager(void)
{
	for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it)
	{
		CPageInfo* buf = *it;
		delete[] buf;
	}
	delete[] m_free_pages;
}

void CPageManager::Init(size_t page_nr)
{
	CPageInfo* buf = new CPageInfo[page_nr];
	m_buffers.push_back(buf);
	m_page_nr += page_nr;
	m_free_pages = new PPAGE[page_nr];
	for (size_t ii = 0; ii < m_page_nr; ++ii)
	{
		m_free_pages[ii] = buf + ii;
	}
}

CPageInfo* CPageManager::get_page(void)
{
	if (m_head_free >= m_page_nr) THROW_ERROR(ERR_APP, L"out of memory for page buffer");
	CPageInfo* page = m_free_pages[m_head_free];
//	memset(page, 0, sizeof(CPageInfo));
	page->Init();
	m_head_free++;
	return page;
}

void CPageManager::put_page(CPageInfo* page)
{
	if (m_head_free == 0)	THROW_ERROR(ERR_APP, L"page buffer is full.");
	m_head_free--;
	m_free_pages[m_head_free] = page;
}

void CPageInfo::Init()
{
	phy_blk = INVALID_BLK;	// page所在物理位置
	// 标记page的温度，当page被写入SSD时更新。这个温度不是实际分配到温度，所有算法下都相同。仅用于统计。
	ttemp = BT_TEMP_NR;
	//在文件中的位置
	inode = nullptr;
	offset = INVALID_BLK;
	// 数据(对于inode 或者 direct node)
	data = nullptr;
	dirty = false;
	type = PAGE_DATA;
	host_write = 0;
	media_write = 0;
}

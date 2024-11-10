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
//	m_buffer.Init();
	// ����free����
	for (UINT ii = 0; ii < m_page_nr; ++ii)
	{
		m_pages[ii].free_link = ii + 1;
		//m_pages[ii].page_id = ii;
//		m_pages[ii].page_id = INVALID_BLK;		// ��־ΪINVALID_BLK�����ڵ��ԣ�����쳣�ͷš�
	}
	m_pages[m_page_nr - 1].free_link = INVALID_BLK;
	m_free_ptr = 0;
	m_used_nr = 0;
}

PAGE_INDEX CPageAllocator::allocate(void)
{
	if (m_used_nr >= m_page_nr) THROW_ERROR(ERR_APP, L"out of memory for page buffer");
	PAGE_INDEX page = m_free_ptr;
	m_free_ptr = m_pages[page].free_link;
	m_used_nr++;

//	if (m_pages[page].page_id != INVALID_BLK) THROW_ERROR(ERR_APP, L"reallocated page, %d", page);
	m_pages[page].Init();
//	m_pages[page].page_id = page;
	return page;
}

CPageInfo* CPageAllocator::allocate(bool data)
{
	PAGE_INDEX pid = allocate();
	CPageInfo* page = &m_pages[pid];
//	CBufferManager::_INDEX bid = m_buffer.get_block();
//	page->data_index = bid;
//	page->page_id = pid;
	return page;
}

//void CPageAllocator::free(PAGE_INDEX page)
void CPageAllocator::free(CPageInfo* page)
{
	if (page == nullptr) THROW_ERROR(ERR_USER, L"page %d has been released.", page);

	PAGE_INDEX page_id = this->page_id(page);
//	if (page == INVALID_BLK) THROW_ERROR(ERR_USER, L"page %d has been released.", page);
//	if (m_pages[page].data_index != INVALID_BLK)
//	{	// ���� data
//		m_buffer.put_block(m_pages[page].data_index);
//	}
	m_pages[page_id].free_link = m_free_ptr;
	m_free_ptr = page_id;
//	m_pages[page].page_id = INVALID_BLK;
	if (m_used_nr == 0) THROW_ERROR(ERR_APP, L"over free page");
	m_used_nr--;
}

BLOCK_DATA* CPageAllocator::get_data(CPageInfo* page)
{
//	if (page->data_index == INVALID_BLK) return nullptr;
//	BLOCK_DATA& blk = m_buffer.get_block_data(page->data_index);
//	return &blk;
	return &(page->data);
}

void CPageInfo::Init()
{
	memset(this, 0xFF, sizeof(CPageInfo));
	phy_blk = INVALID_BLK;	// page��������λ��
	// ���page���¶ȣ���page��д��SSDʱ���¡�����¶Ȳ���ʵ�ʷ��䵽�¶ȣ������㷨�¶���ͬ��������ͳ�ơ�
	ttemp = BT_TEMP_NR;
	//���ļ��е�λ��
	nid = INVALID_BLK;
	offset = INVALID_BLK;
	// ����(����inode ���� direct node)
//	data_index = INVALID_BLK;
	dirty = false;
//	type = PAGE_DATA;
	host_write = 0;
	//media_write = 0;
}

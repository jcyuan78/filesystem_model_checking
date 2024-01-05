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
	phy_blk = INVALID_BLK;	// page��������λ��
	// ���page���¶ȣ���page��д��SSDʱ���¡�����¶Ȳ���ʵ�ʷ��䵽�¶ȣ������㷨�¶���ͬ��������ͳ�ơ�
	ttemp = BT_TEMP_NR;
	//���ļ��е�λ��
	inode = nullptr;
	offset = INVALID_BLK;
	// ����(����inode ���� direct node)
	data = nullptr;
	dirty = false;
	type = PAGE_DATA;
	host_write = 0;
	media_write = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include "../include/storage.h"
#include "../include/f2fs_simulator.h"

CStorage::CStorage(CF2fsSimulator* fs)
{
	m_pages = & fs->m_pages;
//	m_block_buf = &fs->m_block_buf;
	memset(m_data, -1, sizeof(BLOCK_DATA) * TOTAL_BLOCK_NR);
}

void CStorage::Initialize(void)
{
	memset(m_data, -1, sizeof(BLOCK_DATA) * TOTAL_BLOCK_NR);
}

void CStorage::CopyFrom(const CStorage* src)
{
	size_t mem_size = sizeof(BLOCK_DATA) * TOTAL_BLOCK_NR;
	memcpy_s(m_data, mem_size, src->m_data, mem_size);
}

void CStorage::BlockWrite(UINT lba, CPageInfo* page)
{
	JCASSERT(page);
	BLOCK_DATA* block = m_pages->get_data(page);
	if (block)
	{
		memcpy_s(m_data + lba, sizeof(BLOCK_DATA), block, sizeof(BLOCK_DATA));
	}
}

void CStorage::BlockRead(UINT lba, CPageInfo *page)
{
	BLOCK_DATA* block = m_pages->get_data(page);
	if (block)
	{
		memcpy_s(block, sizeof(BLOCK_DATA), m_data+lba, sizeof(BLOCK_DATA));
	}
}



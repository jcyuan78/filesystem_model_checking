///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "f2fs_segment.h"
#include "blocks.h"

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

protected:
//	CPageAllocator::INDEX m_blocks[MAX_PAGE_NUM];
	BLOCK_DATA m_data[TOTAL_BLOCK_NR];
	CPageAllocator* m_pages;
//	CBufferManager* m_block_buf;

};
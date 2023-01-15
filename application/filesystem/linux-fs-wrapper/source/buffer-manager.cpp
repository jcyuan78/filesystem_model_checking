///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "pch.h"

#include "../include/buffer-manager.h"

LOCAL_LOGGER_ENABLE(L"linuxfs.buffer", LOGGER_LEVEL_DEBUGINFO);


CBufferHead::CBufferHead(IVirtualDisk* disk, sector_t block, size_t size)
	: m_disk(disk)
{
	InitializeCriticalSection(&m_lock);
	b_state = 0;
	b_blocknr = block;
//	b_size = size;
	m_secs = BYTE_TO_SECTOR(size);		// blockµÄ´óÐ¡
	m_lba = block * m_secs;
	b_data = new BYTE[size];
}

CBufferHead::~CBufferHead(void)
{
	delete[] b_data;
	DeleteCriticalSection(&m_lock);
}


int CBufferHead::read_slow(void)
{
	bool br = m_disk->ReadSectors(b_data, m_lba, m_secs);
	if (!br) return -1;
	return 0;
}

CBufferManager::CBufferManager(void) : m_disk(NULL)
{
}

CBufferManager::~CBufferManager(void)
{
	RELEASE(m_disk);
}

CBufferHead* CBufferManager::GetBlock(sector_t block, size_t size)
{
	CBufferHead* bh = new CBufferHead(m_disk, block, size);
	return bh;
}



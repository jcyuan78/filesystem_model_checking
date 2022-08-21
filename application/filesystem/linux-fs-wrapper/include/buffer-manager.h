///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "linux_comm.h"
#include <dokanfs-lib.h>

#include "buffer_head.h"


class CBufferHead
{
public:
	CBufferHead(IVirtualDisk* disk, sector_t block, size_t size);
	~CBufferHead(void);

public:
	BYTE* Lock(void)
	{
		EnterCriticalSection(&m_lock);
		return b_data;
	}
	void Unlock(void)
	{
		LeaveCriticalSection(&m_lock);
	}

	bool buffer_uptodate(void) const { return false; }
	int read_slow(void);

	void SetDirty() { set_bit(BH_Dirty, &b_state); }
	int SyncDirty(int op_flat) { JCASSERT(0); return 0; }
	void Release(void) {}
	void AddRef(void) {}

// for debug
	BYTE* Data(void) { return b_data; }

protected:
	CRITICAL_SECTION m_lock;
	UINT32	b_state;
	sector_t b_blocknr;	//	blockÎªblockµØÖ·£¬sector = block * size / sector_size. ²Î¿¼"buffer.c" submit_bh_wbc()
	size_t m_lba;
//	size_t b_size;
	size_t m_secs;
	BYTE* b_data;
	IVirtualDisk* m_disk;
};


class CBufferManager
{
public:
	CBufferManager(void);
	~CBufferManager(void);
public:
	void SetDisk(IVirtualDisk* disk)
	{
		JCASSERT(disk);
		m_disk = disk;
		m_disk->AddRef();
	}
	void Reset(void)
	{
		RELEASE(m_disk);
	}
	CBufferHead* GetBlock(sector_t block, size_t size);


protected:
	IVirtualDisk* m_disk;

};
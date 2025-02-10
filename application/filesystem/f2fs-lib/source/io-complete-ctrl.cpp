///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include <linux-fs-wrapper.h>

#include "../include/io-complete-ctrl.h"
#include "../include/f2fs.h"
#include "../include/f2fs-super-block.h"
#include "../include/f2fs-filesystem.h"

LOCAL_LOGGER_ENABLE(L"f2fs.io", LOGGER_LEVEL_DEBUGINFO);


CIoCompleteCtrl::CIoCompleteCtrl(f2fs_sb_info* sbi, UINT max_qd)
{
	m_max_que_depth = max_qd;
	memset(m_overlaps, 0, sizeof(OVERLAPPED) * MAXIMUM_WAIT_OBJECTS);
	memset(m_events, 0, sizeof(HANDLE) * MAXIMUM_WAIT_OBJECTS);
#ifdef BIO_POOL
	memset(m_bios, 0,  sizeof(bio) * MAXIMUM_WAIT_OBJECTS);
#else
	memset(m_bios, 0, sizeof(bio*) * MAXIMUM_WAIT_OBJECTS);
#endif

#ifdef _DEBUG
	memset(m_free_list, 0xFF, sizeof(int) * MAXIMUM_WAIT_OBJECTS);
#endif
	for (UINT ii = 0; ii < m_max_que_depth; ++ii)
	{
		m_events[ii] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_events[ii] == NULL) THROW_WIN32_ERROR(L"failed on creating event for io, id=%d, max_qd=%d", ii, m_max_que_depth);
//		m_overlaps->hEvent = m_events;
		// add to free
		m_free_list[ii] = ii;
	}
	m_head = 0, m_tail = m_max_que_depth-1;
	InitializeCriticalSection(&m_free_lock);
	InitializeConditionVariable(&m_free_doorbell);

	m_io_buf_size = MAX_IO_BLOCKS * PAGE_SIZE;
	m_total_buf_size = m_io_buf_size *MAXIMUM_WAIT_OBJECTS;
	m_io_buf = (BYTE*)VirtualAlloc(nullptr, m_total_buf_size, MEM_COMMIT, PAGE_READWRITE);
	LOG_TRACK(L"io", L"io data buf, start=%p, end=%p", m_io_buf, m_io_buf + m_total_buf_size);
	m_sbi = sbi;
}

CIoCompleteCtrl::~CIoCompleteCtrl(void)
{
	for (UINT ii = 0; ii < m_max_que_depth; ++ii)
	{
		if (m_events[ii]) CloseHandle(m_events[ii]);
	}
	DeleteCriticalSection(&m_free_lock);
	VirtualFree(m_io_buf, m_total_buf_size, MEM_RELEASE);
}

DWORD CIoCompleteCtrl::Run(void)
{
	do
	{
		InterlockedExchange(&m_started, 1);
		LOG_TRACK(L"io", L"waiting for io complete");
		DWORD ir = WaitForMultipleObjects(m_max_que_depth, m_events, FALSE, INFINITE);
		if (ir > (WAIT_OBJECT_0 + m_max_que_depth)) THROW_WIN32_ERROR(L"failed on waiting io event, ir=%d", ir);
		int io_id = ir - WAIT_OBJECT_0;
		bio* completed_bio = &m_bios[io_id];
//		TRACK_BIO_IO(completed_bio, L"completion, op_id=%d", io_id);
		int op = completed_bio->bi_opf & 0xFF;
		if (op == REQ_OP_READ) ReadCompletionRoutine(0, 0, completed_bio);
		else if (op == REQ_OP_WRITE) WriteCompletionRoutine(0, 0, completed_bio);
		// 回收overlap

#ifdef _DEBUG
		m_overlaps[io_id].hEvent = nullptr;
//		m_bios[io_id] = nullptr;
#endif
		EnterCriticalSection(&m_free_lock);
		bool empty = (m_head == m_tail);		// 释放前，free_list是否是空的。
		m_tail++;
		if (m_tail >= MAXIMUM_WAIT_OBJECTS) m_tail = 0;

#ifdef _DEBUG
		JCASSERT(m_tail != m_head && m_free_list[m_tail] == -1);
#endif
		m_free_list[m_tail] = io_id;
		LeaveCriticalSection(&m_free_lock);
		if (empty)  WakeConditionVariable(&m_free_doorbell);
	} while (m_running);
	return 0;
}

int CIoCompleteCtrl::AllocateOverlap(bio * bb)
{
	EnterCriticalSection(&m_free_lock);
	while (m_tail == m_head)
	{	// wait for overlap
//		TRACK_BIO_IO(bb, L"pending on free bio");
//		LOG_TRACK(L"bio.io", L"pending on free bio");
		SleepConditionVariableCS(&m_free_doorbell, &m_free_lock, INFINITE);
	}

	int index = m_free_list[m_head];
#ifdef _DEBUG
	JCASSERT(index >=0);
	m_free_list[m_head] = -1;
#endif
	m_head++;
	if (m_head >= MAXIMUM_WAIT_OBJECTS) m_head = 0;
	LeaveCriticalSection(&m_free_lock);

#if 1 //def _DEBUG
//	JCASSERT(m_overlaps[index].hEvent == nullptr && m_bios[index] == nullptr);
	m_overlaps[index].hEvent = m_events[index];
#endif

#ifdef BIO_POOL

#else
	m_bios[index] = bb;
#endif
	return index;
}

//void f2fs_sb_info::submit_async_io(bio* bb)
void CIoCompleteCtrl::submit_async_io(bio* bb)
{
	//	JCASSERT(bb && bb->bi_io_vec && bb->bi_bdev);	// bb->bi_io_vec可以为空
	JCASSERT(bb && bb->bi_bdev);
	// 模拟处理block io，暂时做sync read/write
	//	BIO:从bio.bi_iter.bi_sector开始，读取bi_size大小的数据。存入一组page中，page的地址是不连续的。
	//	这对于底层没有问题，可以使用页面映射。对于NVMe类存储器，可以直接回写不同page的数据。
	//	暂时通过复制实现。(1)下一步优化时，在VirtualDisk中实现BIO读写。(2)支持后台读写
	//	结果在bio->bi_status中反应，0：OK, !=0：error
	size_t data_size = bb->bi_iter.bi_size;
	size_t data_secs = BYTE_TO_SECTOR(data_size);
//	JCASSERT(data_secs <= (MAX_IO_BLOCKS * SECTOR_PER_PAGE) );

	// 获取overlap
#ifdef BIO_POOL

#else // BIO_POOL
	int ol_id = AllocateOverlap(bb);
	JCASSERT(ol_id < m_max_que_depth);
	bb->m_ol = m_overlaps + ol_id;
	bb->m_buf = m_io_buf + ol_id * m_io_buf_size;
	TRACK_BIO_IO(bb, L"start_io async, ol_id=%d, buf=%p", ol_id, bb->m_buf);
#endif // BIO_POOL

	size_t offset = SECTOR_TO_BYTE(bb->bi_iter.bi_sector);
	bb->m_ol->Pointer = (PVOID)offset;
	if (data_secs > (MAX_IO_BLOCKS * SECTOR_PER_PAGE))
	{
//		TRACK_BIO_IO(bb, L"apply external buffer, secs=%lld", data_secs);
		LOG_TRACK(L"bio", L"apply external buffer, secs=%lld", data_secs)
		bb->m_buf = new BYTE[data_size];
	}

	int op = bb->bi_opf & 0xFF;
	TRACK_BIO_IO(bb, L"start async bio");

	if (op == REQ_OP_READ)
	{
		bool br = bb->bi_bdev->AsyncReadSectors(bb->m_buf, data_secs, bb->m_ol);
		if (!br) THROW_ERROR(ERR_APP, L"failed on reading data from device, lba=0x%llX, secs=%lld", bb->bi_iter.bi_sector, data_secs);
	}
	else if (op == REQ_OP_WRITE)
	{
		BYTE* buf = bb->m_buf;

		for (WORD ii = 0; ii < bb->bi_vcnt; ++ii)
		{
			bio_vec& vv = bb->bi_io_vec[ii];
			page* pp = vv.bv_page;
			JCASSERT(vv.bv_offset == 0 && vv.bv_len == PAGE_CACHE_SIZE);
			memcpy_s(buf, vv.bv_len, pp->virtual_add, vv.bv_len);
			buf += vv.bv_len;
		}
		bool br = bb->bi_bdev->AsyncWriteSectors(bb->m_buf, data_secs, bb->m_ol);
		if (!br) THROW_ERROR(ERR_APP, L"failed on write data from device, lba=0x%llX, secs=%lld", bb->bi_iter.bi_sector, data_secs);
		m_sbi->m_fs->UpdateDiskWrite(data_secs / 8);
	}
	else
	{
		// for debug
		THROW_ERROR(ERR_APP, L"unknown op code=%d", op);
	}
}

bio* CIoCompleteCtrl::bio_alloc_bioset(gfp_t gfp_mask, unsigned short nr_iovecs)
{
	int index = AllocateOverlap(nullptr);
	bio* bb = m_bios + index;
	memset(bb, 0, sizeof(bio));
	bb->m_ol = m_overlaps + index;
	bb->m_buf = m_io_buf + index * m_io_buf_size;

	gfp_t saved_gfp = gfp_mask;

	/* should not use nobvec bioset for nr_iovecs > 0 */
//	if (WARN_ON_ONCE(!mempool_initialized(&bs->bvec_pool) && nr_iovecs > 0))	return NULL;

	/* submit_bio_noacct() converts recursion to iteration; this means if we're running beneath it, any bios we alloc_obj and submit will not be submitted (and thus freed) until after we return.
	 *
	 * This exposes us to a potential deadlock if we alloc_obj multiple bios from the same bio_set() while running underneath submit_bio_noacct(). If we were to alloc_obj multiple bios (say a stacking block driver that was splitting bios), we would deadlock if we exhausted the mempool's reserve.
	 *
	 * We solve this, and guarantee forward progress, with a rescuer workqueue per bio_set. If we go to alloc_obj and there are bios on current->bio_list, we first try the allocation without __GFP_DIRECT_RECLAIM; if that fails, we punt those bios we would be blocking to the rescuer workqueue before we retry with the original gfp_flags.	 */
	// nr_iovecs=bio包含的page数量
	if (nr_iovecs > BIO_INLINE_VECS)
	{
//		JCASSERT(0);
		bio_vec* bvl = new bio_vec[nr_iovecs];
		bio_init(bb, bvl, nr_iovecs);
	}
	else if (nr_iovecs)	{	bio_init(bb, bb->bi_inline_vecs, BIO_INLINE_VECS);	}
	else				{	bio_init(bb, NULL, 0);	}

	bb->bi_pool = this;
	return bb;
}

void CIoCompleteCtrl::bio_put(bio* bb)
{
	if (bb->bi_max_vecs > BIO_INLINE_VECS) delete[] bb->bi_io_vec;
	bb->bi_io_vec = nullptr;
//	size_t data_size = bb->bi_iter.bi_size;
	size_t data_secs = BYTE_TO_SECTOR(bb->bi_iter.bi_size);
	if (data_secs > (MAX_IO_BLOCKS * SECTOR_PER_PAGE))	{ delete[] bb->m_buf; }
	bb->m_buf = nullptr;
}

void CIoCompleteCtrl::WriteCompletionRoutine(DWORD err_code, DWORD written, bio* bb)
{
//	TRACK_BIO_IO(bb, L"complete write");
	//调用bb->bi_end_io之后，bb被删除不能再被使用。
	if (bb->bi_end_io) (bb->bi_end_io)(bb);
}

void CIoCompleteCtrl::ReadCompletionRoutine(DWORD err_code, DWORD written, bio* bb)
{
//	TRACK_BIO_IO(bb, L"complete read");
	BYTE* buf = bb->m_buf;
	//<优化>能否使得page地址连续，这样可以避免一次内存复制。
	for (WORD ii = 0; ii < bb->bi_vcnt; ++ii)
	{
		bio_vec& vv = bb->bi_io_vec[ii];
		LOG_DEBUG(L"read completed: lba=%d, size=%d", vv.bv_offset, vv.bv_len);
		page* pp = vv.bv_page;
		JCASSERT(vv.bv_offset == 0 && vv.bv_len == PAGE_CACHE_SIZE);
		memcpy_s(pp->virtual_add, vv.bv_len, buf, vv.bv_len);
		buf += vv.bv_len;
	}
	//调用bb->bi_end_io之后，bb被删除不能再被使用。
	if (bb->bi_end_io) (bb->bi_end_io)(bb);
}

void bio_put(bio* bio)
{
	if (bio && bio->bi_pool) bio->bi_pool->bio_put(bio);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "control-thread.h"
//#include "f2fs-super-block.h"

#define BIO_POOL

struct f2fs_sb_info;

class CIoCompleteCtrl : public CControlThread
{
public:
	CIoCompleteCtrl(f2fs_sb_info* sbi, UINT max_qd = MAXIMUM_WAIT_OBJECTS);
	~CIoCompleteCtrl(void);

public:
	void submit_async_io(bio* bb);
	bio* bio_alloc_bioset(gfp_t gfp_mask, unsigned short nr_iovecs);
	void bio_put(bio* bb);


protected:
	virtual DWORD Run(void);
	int AllocateOverlap(bio * bb);		// ���ؿ���overlap������

	//static void WriteCompletionRoutine(DWORD err_code, DWORD written, LPOVERLAPPED overlapped);
	//static void ReadCompletionRoutine(DWORD err_code, DWORD written, LPOVERLAPPED overlapped);

	static void WriteCompletionRoutine(DWORD err_code, DWORD written, bio* bb);
	static void ReadCompletionRoutine(DWORD err_code, DWORD written, bio* bb);


protected:
	UINT m_max_que_depth;

	HANDLE m_events[MAXIMUM_WAIT_OBJECTS];
	OVERLAPPED m_overlaps[MAXIMUM_WAIT_OBJECTS];
#ifdef BIO_POOL
	bio m_bios[MAXIMUM_WAIT_OBJECTS];
#else
//	bio* m_bios[MAXIMUM_WAIT_OBJECTS];
#endif
	size_t m_io_buf_size, m_total_buf_size;	// һ��buf�Ĵ�С��������buf�Ĵ�С
	BYTE* m_io_buf;	// ����device��ioʱ����page���ݸ��Ƶ�buf��

	// ���й������Overlapped��HANDLE���±ꡣ
	int m_free_list[MAXIMUM_WAIT_OBJECTS];
	int m_head, m_tail;
	CRITICAL_SECTION m_free_lock;
	CONDITION_VARIABLE m_free_doorbell;	// ��free list�ɿձ�ɷǿ�ʱ֪ͨ��
	f2fs_sb_info * m_sbi;

};
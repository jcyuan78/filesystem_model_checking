///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

//#include <linux-fs-wrapper.h>
#include "../include/f2fs_fs.h"
#include "../include/f2fs.h"

LOCAL_LOGGER_ENABLE(L"f2fs.checkpoint_ctrl", LOGGER_LEVEL_DEBUGINFO);

#define DEFAULT_CHECKPOINT_IOPRIO (IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 3))


ckpt_req_control::ckpt_req_control(f2fs_sb_info * sbi)
{
	f2fs_init_ckpt_req_control(sbi);
	InitializeCriticalSection(&stat_lock);
	InitializeCriticalSection(&m_req_list_lock);
	m_wait = CreateEvent(NULL, FALSE, FALSE, NULL);
#ifdef _DEBUG
	m_thread_name = L"checkpoint control";
#endif 
}

void ckpt_req_control::f2fs_init_ckpt_req_control(f2fs_sb_info* sbi)
{
	m_sbi = sbi;
	//struct ckpt_req_control* cprc = &sbi->cprc_info;

	atomic_set(&issued_ckpt, 0);
	atomic_set(&total_ckpt, 0);
	atomic_set(&queued_ckpt, 0);
	ckpt_thread_ioprio = DEFAULT_CHECKPOINT_IOPRIO;
#if 0	// 检查ckpt_wait_queue的目的
	init_waitqueue_head(&ckpt_wait_queue);
#endif
	init_llist_head(&issue_list);
	spin_lock_init(&stat_lock);

}

ckpt_req_control::~ckpt_req_control(void)
{
	DeleteCriticalSection(&m_req_list_lock);
	DeleteCriticalSection(&stat_lock);
	CloseHandle(m_wait);
}

DWORD ckpt_req_control::issue_checkpoint_thread(void)
{
	//<YUAN> Linux内核中，需要程序自己调度线程。wait queue或许是用来调度线程的。如果这样，Windows的user mode中就不需要了。
	// 	   用event代替wait queue来唤醒线程。
//	struct ckpt_req_control* cprc = &sbi->cprc_info;
//	wait_queue_head_t* q = ckpt_wait_queue;
	while (m_running)
	{
	//需要处理如何让线程结束
		//if (kthread_should_stop())	return 0;
		bool empty;
		EnterCriticalSection(&m_req_list_lock);
		empty = llist_empty(&issue_list);
		LeaveCriticalSection(&m_req_list_lock);
		if (!empty) 	__checkpoint_and_complete_reqs();

//		wait_event_interruptible(*q, kthread_should_stop() || !llist_empty(&issue_list));
		//<YUAN> 用event代替wait queue
		DWORD ir = WaitForSingleObject(m_wait, INFINITE);
		if (ir != 0)	THROW_ERROR(ERR_APP, L"failed on waiting checkpoint thread, err=%d", ir);
		//goto repeat;
	}
	return 0;
}

void ckpt_req_control::__checkpoint_and_complete_reqs(void)
{
	//	struct ckpt_req_control *cprc = &sbi->cprc_info;
	ckpt_req* req, * next;
	llist_node* dispatch_list;
	u64 sum_diff = 0, diff, count = 0;
	int ret;
	EnterCriticalSection(&m_req_list_lock);
	dispatch_list = llist_del_all(&issue_list);
	LeaveCriticalSection(&m_req_list_lock);
	if (!dispatch_list)		return;
	dispatch_list = llist_reverse_order(dispatch_list);

	ret = __write_checkpoint_sync();
	atomic_inc(&issued_ckpt);

	llist_for_each_entry_safe(ckpt_req, req, next, dispatch_list, llnode)
	{
		//<YUAN> TODO: 换算成ms
		diff = jcvos::GetTimeStamp() - req->queue_time;
		//		diff = (u64)ktime_ms_delta(ktime_get(), req->queue_time);
		req->ret = ret;
//		complete(&req->wait);
		req->Complete();
		sum_diff += diff;
		count++;
	}
	atomic_sub(boost::numeric_cast<long>(count), &queued_ckpt);
	atomic_add(boost::numeric_cast<long>(count), &total_ckpt);

	spin_lock(&stat_lock);
	cur_time = (unsigned int)(sum_diff / count);//div64_u64(sum_diff, count);
	if (peak_time < cur_time)		peak_time = cur_time;
	spin_unlock(&stat_lock);
}

void ckpt_req_control::AddRequest(ckpt_req* req)
{
	LOG_STACK_TRACE();
	EnterCriticalSection(&m_req_list_lock);
	llist_add(&req->llnode, &issue_list);
	LeaveCriticalSection(&m_req_list_lock);
	atomic_inc(&queued_ckpt);
	WakeUp();
}
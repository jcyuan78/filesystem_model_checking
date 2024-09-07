///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"
#include <Psapi.h>
#include <boost/property_tree/json_parser.hpp>

LOCAL_LOGGER_ENABLE(L"extester.multithread", LOGGER_LEVEL_DEBUGINFO);

//#define MAX_OPERATION 100

#ifdef THREAD_QUEUE
#define DoFsOperator_ DoFsOperator_Queue
#elif defined THREAD_POOL
#define DoFsOperator_ DoFsOperator_Pool
#elif defined THREAD_MULTI
#define DoFsOperator_ DoFsOperator_Thread
#endif

ERROR_CODE CExTester::EnumerateOp_Thread(CFsState* cur_state, std::list<CFsState*>::iterator& insert)
{
	LOG_STACK_TRACE();
	UINT context_id = 0;
	UINT first_op = m_op_sn;

	CReferenceFs& ref_fs = cur_state->m_ref_fs;
	auto endit = ref_fs.End();
	auto it = ref_fs.Begin();
	TRACE_ENTRY op;

	for (; it != endit; it++)
	{
		if (context_id >= MAX_WORK_NR) THROW_ERROR(ERR_APP, L"too many operations");
		const CReferenceFs::CRefFile& file = ref_fs.GetFile(it);
		bool isdir = ref_fs.IsDir(file);

		std::string path;
		ref_fs.GetFilePath(file, path);

		DWORD checksum;
		FSIZE file_len;
		ref_fs.GetFileInfo(file, checksum, file_len);


		if (isdir)
		{	// 目录
			UINT child_num = file.child_num();
			if (child_num >= m_max_child_num) continue;
			if (file.depth() >= (m_max_dir_depth - 1)) continue;
			if (ref_fs.GetFileNumber() >= MAX_FILE_NUM) continue;

			char fn[3];	//文件名，随机产生2字符
			GenerateFn(fn, 2);
			op.file_path = (path.size() > 1) ? (path + "\\" + fn) : (path + fn);
			op.op_code = OP_FILE_CREATE;
			DoFsOperator_(cur_state, op, m_works + (context_id++));

			GenerateFn(fn, 2);
			op.file_path = (path.size() > 1) ? (path + "\\" + fn) : (path + fn);
			op.op_code = OP_DIR_CREATE;
			DoFsOperator_(cur_state, op, m_works + (context_id++));
		}
		else
		{	// 文件
			FSIZE offset = (FSIZE)(file_len * ((float)(rand()) / RAND_MAX));
			FSIZE len = (FSIZE)((m_max_file_size - offset) * ((float)(rand()) / RAND_MAX));
			if (file.is_open())
			{
				op.fid = file.get_fid();
				if (file.write_count() < m_max_file_op)
				{
					op.op_code = OP_FILE_WRITE;
					op.file_path = path;
					op.offset = offset;
					op.length = len;
					DoFsOperator_(cur_state, op, m_works + (context_id++));
				}

				op.op_code = OP_FILE_CLOSE;
				op.file_path = path;
				DoFsOperator_(cur_state, op, m_works + (context_id++));
			}
			else
			{
				op.op_code = OP_FILE_DELETE;
				op.file_path = path;
				DoFsOperator_(cur_state, op, m_works + (context_id++));

				op.op_code = OP_FILE_OPEN;
				op.file_path = path;
				DoFsOperator_(cur_state, op, m_works + (context_id++));
			}
		}
//		if (context_id > MAX_WORK_NR) THROW_ERROR(ERR_APP, L"context overflow");
	}
	op.op_code = OP_DEMOUNT_MOUNT;
	DoFsOperator_(cur_state, op, m_works + (context_id++));

	if (context_id > m_max_work) m_max_work = context_id;
	// 等待线程结束
	if (context_id == 0) return ERR_OK;



#ifdef THREAD_QUEUE
	for (UINT ii = 0; ii < context_id; ++ii)
	{
		DWORD ir = WaitForSingleObject(m_cmp_doorbell, 10000);
		if (ir != 0) THROW_WIN32_ERROR(L"Time out on wating fs operation");
		//		LOG_DEBUG(L"op completed =%d, sub que=%zd, cmp que=%zd", ii, m_sub_q.size(), m_cmp_q.size());
		//		EnterCriticalSection(&m_cmp_crit);
		//		m_cmp_q.pop_front();
		//		LeaveCriticalSection(&m_cmp_crit);
	}
#else
	// 等待所有任务完成，并且保持搜索结果稳定
	DWORD ir = WaitForMultipleObjects(context_id, m_work_events, TRUE, 10000);
	if (ir >= context_id) THROW_WIN32_ERROR(L"Time out on wating fs operation");

#if 0
	// 不保持结果稳定性
	int ii = context_id;
	while (ii > 0)
	{
		DWORD ir = WaitForMultipleObjects(context_id, m_work_events, FALSE, 10000);
		if (ir >= (context_id)) THROW_WIN32_ERROR(L"Time out on wating fs operation");
		//		{	// 处理测试结果
		if (m_works[ir].ir != ERR_OK)
		{	// error handling
			THROW_ERROR(ERR_APP, L"test failed, code=%d", m_works[ir].ir);
		}
		CFsState* state = m_works[ir].state;
		JCASSERT(state);
		if (m_closed.Check(state))
		{
			m_states.put(state);
		}
		else m_open_list.insert(insert, state);
		m_works[ir].ir = ERR_UNKNOWN;
		ii--;
	}
#elif 0
	for (int ii = 0; ii < context_id; ++ii)
	{
		DWORD ir = WaitForSingleObject(m_works[ii].hevent, 10000);
		if (ir != 0) THROW_WIN32_ERROR(L"Time out on wating fs operation");
		int index = ii;
	}
#endif

#endif

	// 保持搜索结果稳定
	for (UINT ii = 0; ii < context_id; ++ii)
	{
		if (m_works[ii].ir != ERR_OK)
		{	// error handling
//			THROW_ERROR(ERR_APP, L"test failed, code=%d", m_works[ii].ir);
			OutputTrace(m_works[ii].state);
			m_states.put(m_works[ii].state);
//			return m_works[ii].ir;
			continue;
		}
		CFsState* state = m_works[ii].state;
		if (m_closed.Check(state))
		{
			m_states.put(state);
			//m_works[index].state = nullptr;
		}
		else m_open_list.insert(insert, state);
		m_works[ii].ir = ERR_UNKNOWN;
		// 更新file system参数
		UpdateFsParam(m_works[ii].state->m_real_fs);
	}

	//	double runtime = RUNNING_TIME;
	//	fprintf_s(m_log_performance, "%d,%.1f,%d,%.1f\n", first_op, runtime, context_id, runtime/context_id);
	return ERR_OK;
}

bool CExTester::DoFsOperator_Queue(CFsState* cur_state, TRACE_ENTRY& op, WORK_CONTEXT* context)
{
	LOG_STACK_PERFORM(L"");
	//	context->state = m_states.duplicate(cur_state);
	context->src_state = cur_state;
	InterlockedIncrement(&(cur_state->m_ref));

	context->state = m_states.get();
	context->state->m_op = op;
	context->state->m_op.op_sn = m_op_sn++;
	context->ir = ERR_PENDING;
	LOG_DEBUG(L"submit work: context=%p, op=%d, code=%d, work=%p", context, m_op_sn - 1, op.op_code, context->work_item);
	EnterCriticalSection(&m_sub_crit);
	m_sub_q.push_back(context);
	LeaveCriticalSection(&m_sub_crit);
	ReleaseSemaphore(m_sub_doorbell, 1, nullptr);
	return true;
}

bool CExTester::DoFsOperator_Thread(CFsState* cur_state, TRACE_ENTRY& op, WORK_CONTEXT* context)
{
	LOG_STACK_PERFORM(L"");
	context->src_state = cur_state;
	InterlockedIncrement(&(cur_state->m_ref));

	context->state = m_states.get();
	context->state->m_op = op;
	context->state->m_op.op_sn = m_op_sn++;
	context->ir = ERR_PENDING;
	LOG_DEBUG(L"submit work: context=%p, op=%d, code=%d, work=%p", context, m_op_sn - 1, op.op_code, context->work_item);
	SetEvent(context->hstart);
	return true;
}

bool CExTester::DoFsOperator_Pool(CFsState* cur_state, TRACE_ENTRY& op, WORK_CONTEXT* context)
{
	LOG_STACK_PERFORM(L"");
	//	context->state = m_states.duplicate(cur_state);
	context->src_state = cur_state;
	InterlockedIncrement(&(cur_state->m_ref));

	context->state = m_states.get();
	context->state->m_op = op;
	context->state->m_op.op_sn = m_op_sn++;
	context->ir = ERR_PENDING;
	LOG_DEBUG(L"submit work: context=%p, op=%d, code=%d, work=%p", context, m_op_sn - 1, op.op_code, context->work_item);
	//	BOOL br = TrySubmitThreadpoolCallback(FsOperator_Callback, context , &m_tp_environ);
	SubmitThreadpoolWork(context->work_item);
	return true;
}

DWORD CExTester::FsOperator_Queue(void)
{
	while (1)
	{
		DWORD ir = WaitForSingleObject(m_sub_doorbell, INFINITE);
		if (ir != 0) THROW_WIN32_ERROR(L"failed on waiting submit doorbell");
		EnterCriticalSection(&m_sub_crit);
		WORK_CONTEXT* context = m_sub_q.front();
		m_sub_q.pop_front();
		LeaveCriticalSection(&m_sub_crit);
		FsOperator_Callback(context);

		//		EnterCriticalSection(&m_cmp_crit);
		//		m_cmp_q.push_back(context);
		//		LeaveCriticalSection(&m_cmp_crit);
		ReleaseSemaphore(m_cmp_doorbell, 1, nullptr);
	}
	return 0;
}

DWORD __stdcall CExTester::FsOperator_Thread(PVOID _context)
{
	WORK_CONTEXT* context = (WORK_CONTEXT*)(_context);
	while (1)
	{
		WaitForSingleObject(context->hstart, INFINITE);
		FsOperator_Callback(context);
		SetEvent(context->hevent);
	}
	return 0;
}

VOID CExTester::FsOperator_Pool(PTP_CALLBACK_INSTANCE instance, PVOID _context, PTP_WORK work)
{
	LOG_STACK_PERFORM(L"");
	//	LOG_DEBUG(L"start work: context=%p, work=%p", context, work);
	WORK_CONTEXT* context = (WORK_CONTEXT*)(_context);
	FsOperator_Callback(context);
	SetEvent(context->hevent);
}

VOID CExTester::FsOperator_Callback(WORK_CONTEXT* context)
{
	//	WORK_CONTEXT* context = (WORK_CONTEXT*)(_context);
	JCASSERT(context);
	CExTester* tester = context->tester;
	context->state->DuplicateFrom(context->src_state);
	TRACE_ENTRY& op = context->state->m_op;
	LOG_DEBUG(L"start work: context=%p, op=%d, code=%d", context, op.op_sn, op.op_code);

	ERROR_CODE ir;
	try {
		ir = tester->FsOperatorCore(context->state, op);
	}
	catch (jcvos::CJCException& err)
	{
		printf_s("Test failed with exception: %S\n", err.WhatT());
		ir = ERR_GENERAL;
//		tester->OutputTrace(context->state);
	}
	context->ir = ir;
	LOG_DEBUG(L"complete work: context=%p, op=%d, code=%d", context, op.op_sn, op.op_code);

	//	SetEvent(context->hevent);
}


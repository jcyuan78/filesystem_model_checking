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

ERROR_CODE CExTester::EnumerateOp_Thread(std::vector<TRACE_ENTRY>& ops, CFsState* cur_state, std::list<CFsState*>::iterator& insert)
{
	LOG_STACK_TRACE();
	size_t op_nr = GenerateOps(cur_state, ops);

	UINT context_id = 0;
	for (size_t ii = 0; ii < op_nr; ++ii)
	{
		DoFsOperator_(cur_state, ops[ii], m_works + (context_id++));
	}

#if 0
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
#endif;
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
		if (m_works[ir].result != ERR_OK)
		{	// error handling
			THROW_ERROR(ERR_APP, L"test failed, code=%d", m_works[ir].result);
		}
		CFsState* state = m_works[ir].state;
		JCASSERT(state);
		if (m_closed.Check(state))
		{
			m_states.put(state);
		}
		else m_open_list.insert(insert, state);
		m_works[ir].result = ERR_UNKNOWN;
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
		if (m_works[ii].result != ERR_OK)
		{	// error handling
			OutputTrace(m_works[ii].state);
			m_states.put(m_works[ii].state);
			continue;
		}
		CFsState* state = m_works[ii].state;
		if (m_closed.Check(state))
		{
			m_states.put(state);
		}
		else m_open_list.insert(insert, state);
		m_works[ii].result = ERR_UNKNOWN;
		// 更新file system参数
		UpdateFsParam(m_works[ii].state->m_real_fs);
	}

	//	double runtime = RUNNING_TIME;
	//	fprintf_s(m_log_performance, "%d,%.1f,%d,%.1f\n", first_op, runtime, context_id, runtime/context_id);
	return ERR_OK;
}

ERROR_CODE CExTester::EnumerateOp_Thread_V2(std::vector<TRACE_ENTRY>& ops, CFsState* cur_state, 
		std::list<CFsState*>::iterator& insert)
{
	LOG_STACK_TRACE();
	size_t op_nr = GenerateOps(cur_state, ops);

	UINT context_id = 0;
//	LOG_DEBUG(L"op num= %d", ops.size());
	// 从可能的操作中随机选择一个操作，并执行
	for (int ii = 0; ii < m_branch; ++ii)
	{
		int index = rand() % op_nr;
		TRACE_ENTRY& op = ops[index];
		DoFsOperator_(cur_state, op, m_works + (context_id++));
	}

	// 等待线程结束
	if (context_id > m_max_work) m_max_work = context_id;
	if (context_id == 0) return ERR_OK;

#ifdef THREAD_QUEUE
	ERROR_CODE err = ERR_OK;
	for (UINT ii = 0; ii < context_id; ++ii)
	{
		if (WaitForSingleObject(m_cmp_doorbell, INFINITE) != 0) {
			THROW_WIN32_ERROR(L"Time out on wating fs operation");
		}
		//		LOG_DEBUG(L"op completed =%d, sub que=%zd, cmp que=%zd", ii, m_sub_q.size(), m_cmp_q.size());
		EnterCriticalSection(&m_cmp_crit);
		WORK_CONTEXT * result = m_cmp_q.front();
		m_cmp_q.pop_front();
		LeaveCriticalSection(&m_cmp_crit);
		CFsState* state = result->state;

		char str[256];
		Op2String(str, state->m_op);
		LOG_DEBUG(L"operation: %S", str);

		if (result->result != ERR_OK)
		{	// error handling
			m_states.put(state);
		}
		else {
			if (m_closed.Check(state))		{		m_states.put(state);	}
			else		{
				m_open_list.insert(insert, state);
				UpdateFsParam(state->m_real_fs);
			}
		}
		result->result = ERR_UNKNOWN;
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
		if (m_works[ir].result != ERR_OK)
		{	// error handling
			THROW_ERROR(ERR_APP, L"test failed, code=%d", m_works[ir].result);
		}
		CFsState* state = m_works[ir].state;
		JCASSERT(state);
		if (m_closed.Check(state))
		{
			m_states.put(state);
		}
		else m_open_list.insert(insert, state);
		m_works[ir].result = ERR_UNKNOWN;
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
	return err;
}


bool CExTester::DoFsOperator_Queue(CFsState* cur_state, TRACE_ENTRY& op, WORK_CONTEXT* context)
{
	LOG_STACK_PERFORM(L"");
	//	context->state = m_states.duplicate(cur_state);
	context->src_state = cur_state;
	cur_state->add_ref();
//	InterlockedIncrement(&(cur_state->m_ref));

	context->state = m_states.get();
	context->state->m_op = op;
	context->state->m_op.op_sn = m_op_sn++;
	context->result = ERR_PENDING;
	LOG_DEBUG_(1, L"submit work: context=%p, op=%d, code=%d", context, m_op_sn - 1, op.op_code);
	EnterCriticalSection(&m_sub_crit);
	m_sub_q.push_back(context);
	LeaveCriticalSection(&m_sub_crit);
	ReleaseSemaphore(m_sub_doorbell, 1, nullptr);
	return true;
}

bool CExTester::DoFsOperator_Thread(CFsState* cur_state, TRACE_ENTRY& op, WORK_CONTEXT* context)
{
#ifdef MULTI_THREAD
	LOG_STACK_PERFORM(L"");
	context->src_state = cur_state;
	InterlockedIncrement(&(cur_state->m_ref));

	context->state = m_states.get();
	context->state->m_op = op;
	context->state->m_op.op_sn = m_op_sn++;
	context->result = ERR_PENDING;
	LOG_DEBUG(L"submit work: context=%p, op=%d, code=%d, work=%p", context, m_op_sn - 1, op.op_code, context->work_item);
	SetEvent(context->hstart);
#endif
	return true;
}

bool CExTester::DoFsOperator_Pool(CFsState* cur_state, TRACE_ENTRY& op, WORK_CONTEXT* context)
{
#ifdef THREAD_POOL
	LOG_STACK_PERFORM(L"");
	//	context->state = m_states.duplicate(cur_state);
	context->src_state = cur_state;
	InterlockedIncrement(&(cur_state->m_ref));

	context->state = m_states.get();
	context->state->m_op = op;
	context->state->m_op.op_sn = m_op_sn++;
	context->result = ERR_PENDING;
	LOG_DEBUG(L"submit work: context=%p, op=%d, code=%d, work=%p", context, m_op_sn - 1, op.op_code, context->work_item);
	//	BOOL br = TrySubmitThreadpoolCallback(FsOperator_Callback, context , &m_tp_environ);
	SubmitThreadpoolWork(context->work_item);
#endif
	return true;
}

DWORD CExTester::FsOperator_Queue(void)
{
	// 初始化随机数
	DWORD tid = GetCurrentThreadId();
	srand(tid);
	while (1)
	{
		DWORD ir = WaitForSingleObject(m_sub_doorbell, INFINITE);
		if (ir != 0) THROW_WIN32_ERROR(L"failed on waiting submit doorbell");
		EnterCriticalSection(&m_sub_crit);
		WORK_CONTEXT* context = m_sub_q.front();
		m_sub_q.pop_front();
		LeaveCriticalSection(&m_sub_crit);
		FsOperator_Callback(context);

		EnterCriticalSection(&m_cmp_crit);
		m_cmp_q.push_back(context);
		LeaveCriticalSection(&m_cmp_crit);
		ReleaseSemaphore(m_cmp_doorbell, 1, nullptr);
	}
	return 0;
}

DWORD __stdcall CExTester::FsOperator_Thread(PVOID _context)
{
#ifdef MULTI_THREAD
	WORK_CONTEXT* context = (WORK_CONTEXT*)(_context);
	while (1)
	{
		WaitForSingleObject(context->hstart, INFINITE);
		FsOperator_Callback(context);
		SetEvent(context->hevent);
	}
#endif
	return 0;
}

VOID CExTester::FsOperator_Pool(PTP_CALLBACK_INSTANCE instance, PVOID _context, PTP_WORK work)
{
#ifdef THREAD_POOL
	LOG_STACK_PERFORM(L"");
	//	LOG_DEBUG(L"start work: context=%p, work=%p", context, work);
	WORK_CONTEXT* context = (WORK_CONTEXT*)(_context);
	FsOperator_Callback(context);
	SetEvent(context->hevent);
#endif
}

VOID CExTester::FsOperator_Callback(WORK_CONTEXT* context)
{
	JCASSERT(context);
	CExTester* tester = context->tester;
	context->state->DuplicateFrom(context->src_state);
	TRACE_ENTRY& op = context->state->m_op;
//	LOG_DEBUG(L"start work: context=%p, op=%d, code=%d", context, op.op_sn, op.op_code);

	ERROR_CODE ir;
	std::string err_msg;
	try {
		ir = tester->FsOperatorCore(context->state, op);
	}
	catch (jcvos::CJCException& err)
	{
		err_msg = err.what();
		printf_s("Test failed with exception: %s\n", err_msg.c_str());
		ir = ERR_GENERAL;
	}
	if (ir != ERR_OK)
	{
		tester->OutputTrace_Thread(context->state, ir, err_msg);
	}
	context->result = ir;
//	LOG_DEBUG(L"complete work: context=%p, op=%d, code=%d", context, op.op_sn, op.op_code);
}

bool CExTester::OutputTrace_Thread(CFsState* state, ERROR_CODE ir, const std::string& err, DWORD tid)
{
	LOG_STACK_PERFORM(L"OutputTrace");
//	EnterCriticalSection(&m_trace_crit);
	if (tid == 0) tid = GetCurrentThreadId();
	char str_fn[MAX_PATH];
	sprintf_s(str_fn, "%S\\error_log_%d.txt", m_log_path.c_str(), tid);
	FILE* fp = nullptr;
	fopen_s(&fp, str_fn, "w+");
	fprintf_s(fp, "error code=%d, due to: %s\n", ir, err.c_str());
	sprintf_s(str_fn, "%S\\trace_%d.json", m_log_path.c_str(), tid);
	DWORD option = TRACE_REF_FS | TRACE_REAL_FS;
	if (ir != ERR_OK) {
		option |= (TRACE_FILES | /*TRACE_ENCODE |*/ TRACE_JSON);
	}
	OutputTrace(fp, str_fn, state, option);
	fclose(fp);

	printf_s("dump trace for:%s to %s\n", err.c_str(), str_fn);
//	LeaveCriticalSection(&m_trace_crit);
	return true;
}

bool CExTester::OutputTrace(FILE* fp, const std::string& json_fn, CFsState* state, DWORD option)
{
	std::list<CFsState*> stack;
	char str_encode[MAX_ENCODE_SIZE];
	memset(str_encode, 0, sizeof(str_encode));
	while (state)
	{
		FSIZE logic_blk = 0;
		fprintf_s(fp, "state=%p, parent=%p, depth=%d\n", state, state->m_parent, state->m_depth);
		IFsSimulator* fs = state->m_real_fs;
		CReferenceFs& ref_fs = state->m_ref_fs;

		if (option & TRACE_REAL_FS) {
			FS_INFO fs_info;
			fs->GetFsInfo(fs_info);
			fprintf_s(fp,"\treal_fs: dir=%d, files=%d, logic=%d, physic=%d, seg:free/total=%d / %d, blk:free/total=%d / %d, \n",
				0, 0, fs_info.used_blks, fs_info.physical_blks, fs_info.free_seg, fs_info.total_seg, 
				fs_info.free_blks, fs_info.total_blks);
//			fprintf_s(fp, "\tfs: encode=%s, free pages= %d / %d\n",
//				str_encode, fs_info.free_page_nr, fs_info.total_page_nr);
		}
		if (option & TRACE_REF_FS) {
			UINT ref_dir_nr = ref_fs.m_dir_num, ref_file_nr = ref_fs.m_file_num;
			if (option & TRACE_ENCODE)	ref_fs.Encode(str_encode, MAX_ENCODE_SIZE);
			else str_encode[0] = 0;
			fprintf_s(fp, "\tref_fs: dir=%d, file=%d, encode=%s\n ", ref_dir_nr, ref_file_nr, str_encode);
		}

		if (option & TRACE_FILES) {
			auto endit = ref_fs.End();
			for (auto it = ref_fs.Begin(); it != endit; ++it)
			{
				const CReferenceFs::CRefFile& ref_file = ref_fs.GetFile(it);
				if (option & TRACE_ENCODE)		ref_file.GetEncodeString(str_encode, MAX_ENCODE_SIZE);

				std::string path;
				ref_fs.GetFilePath(ref_file, path);
				bool dir = ref_fs.IsDir(ref_file);
				FSIZE ref_len = 0;
				if (!dir)
				{
					DWORD ref_checksum;
					ref_fs.GetFileInfo(ref_file, ref_checksum, ref_len);
				}
				static const size_t str_size = (INDEX_TABLE_SIZE * 10);
#if 0
				char str_index[str_size];
				NID index[INDEX_TABLE_SIZE];		// 磁盘数据，
				size_t nr = fs->DumpFileIndex(index, INDEX_TABLE_SIZE, ref_file.get_fid());
				size_t ptr = 0;
				for (size_t ii = 0; ii < INDEX_TABLE_SIZE; ++ii)
				{
					if (index[ii] == INVALID_BLK) break;
					ptr += sprintf_s(str_index + ptr, str_size - ptr, "%03X ", index[ii]);
			}
				str_index[0] = 0;
#endif
				FSIZE file_size = ref_len, file_blk_nr = 0, file_index_nr = 0;		// 文件占用的block数量，包括inode和index node
				logic_blk += (file_blk_nr + file_index_nr);
				fprintf_s(fp, "\t\t<check %s> (fid=%03d) %s,\t\t blk_nr=%d, size=%d, (%s)\n",
					dir ? "dir " : "file", ref_file.get_fid(), path.c_str(), (file_blk_nr + file_index_nr), file_size,
					str_encode);
			}
		}
		if (option & TRACE_GC) {
			//std::vector<GC_TRACE> gc;
			//fs->GetGcTrace(gc);
			//if (!gc.empty())
			//{
			//	fprintf_s(fp,"gc:\n");
			//	for (auto ii = gc.begin(); ii != gc.end(); ++ii)
			//	{
			//		fprintf_s(fp,"(%d,%d): %d=>%d\n", ii->fid, ii->offset, ii->org_phy, ii->new_phy);
			//	}
			//}
		}
//		fprintf_s(fp, "\ttotal logic block=%d\n", logic_blk);
		char str[256];
		Op2String(str, state->m_op);
		fprintf_s(fp,"\t%s\n", str);
		if (option & TRACE_JSON)	stack.push_front(state);
		state = state->m_parent;
	}

	if (option & TRACE_JSON) {

		// 将trace 转化为json文件，便于后续调试
		boost::property_tree::ptree prop_trace;
		boost::property_tree::ptree prop_op_array;

		int step = 0;
		for (auto ii = stack.begin(); ii != stack.end(); ++ii, ++step)
		{
			TRACE_ENTRY& op = (*ii)->m_op;
			// op to propeyty
			boost::property_tree::ptree prop_op;
			prop_op.add("step", step);
			prop_op.add<std::string>("op_name", OpName(op.op_code));
			prop_op.add<std::string>("path", op.file_path);
			prop_op.add("offset", op.offset);
			prop_op.add("length", op.length);
			prop_op_array.push_back(std::make_pair("", prop_op));
		}
		prop_trace.add_child("op", prop_op_array);

		boost::property_tree::write_json(json_fn, prop_trace);
	}

	return true;
}

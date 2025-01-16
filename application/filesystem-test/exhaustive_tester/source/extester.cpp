///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"
#include <Psapi.h>
#include <boost/property_tree/json_parser.hpp>

LOCAL_LOGGER_ENABLE(L"extester", LOGGER_LEVEL_DEBUGINFO);


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Help functions ==
int GenerateFn(char* fn, int len)
{
	int ii;
	int fn_len = rand() % len + 1;
	for (ii = 0; ii < fn_len; ++ii)
	{
		fn[ii] = rand() % 26 + 'A';
	}
	fn[ii] = 0;
	return ii;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Exahustive Tester ==
CExTester::CExTester(void)
{
	m_states.Initialize(0);
}

CExTester::~CExTester(void)
{
}

int CExTester::PrepareTest(const boost::property_tree::wptree& config, IFsSimulator * fs, const std::wstring & log_path)
{
	JCASSERT(fs);
	m_fs_factory = fs;
	m_timeout = config.get<DWORD>(L"timeout", INFINITE);
	m_update_ms = config.get<DWORD>(L"message_interval", 30);		// 以秒为单位的更新时间
	m_format = config.get<bool>(L"format_before_test", false);
	m_mount = config.get<bool>(L"mount_before_test", false);
	m_test_depth = config.get<int>(L"depth", 10);
	m_max_child_num = config.get<size_t>(L"max_child", 5);
	m_max_dir_depth = config.get<size_t>(L"max_dir_depth", 5);
	m_max_file_op = config.get<size_t>(L"max_file_op", 5);
	m_thread_num = config.get<UINT>(L"thread_num", 8);
	m_branch = config.get<int>(L"branch", -1);
	m_check_power_loss = config.get<bool>(L"check_power_loss", false);

	// 文件系统支持的最大文件大小。
	FSIZE max_file_secs = m_fs_factory->MaxFileSize() * BLOCK_SIZE;
	m_max_file_size = config.get<FSIZE>(L"max_file_size", 8388608);		// 128 * 128 block * 512B
	if (m_max_file_size > max_file_secs)
	{
		LOG_WARNING(L"[warning] max file size %zd (secs) > file system supported %zd (secs)", m_max_file_size, max_file_secs);
		m_max_file_size = max_file_secs;
	}

	m_clear_temp = config.get<int>(L"clear_temp", 1);
	// 读取测试项目
	const boost::property_tree::wptree& pt_ops = config.get_child(L"operaters");
	for (auto it = pt_ops.begin(); it != pt_ops.end(); it++)
	{
		if (it->first != L"operater") continue;
		const boost::property_tree::wptree& pt_op = it->second;
		const std::wstring& str_op = pt_op.get<std::wstring>(L"<xmlattr>.name");
		OP_CODE op_id = StringToOpId(str_op);
		const std::wstring& str_cond = pt_op.get<std::wstring>(L"<xmlattr>.condition");
		if (str_cond == L"File") m_file_op_set.push_back(op_id);
		else if (str_cond == L"Dir") m_dir_op_set.push_back(op_id);
		else if (str_cond == L"FS") m_fs_op_set.push_back(op_id);
	}

	// 设置log文件夹
	m_log_path = log_path;
	std::wstring trace_log_fn = m_log_path + L"\\log.txt";
#ifdef ENABLE_LOG
	m_log_file = _wfsopen(trace_log_fn.c_str(), L"w+", _SH_DENYNO);
	if (!m_log_file) THROW_ERROR(ERR_USER, L"failed on opening log file %s", log_path.c_str());
#else
	m_log_file = nullptr;
#endif
	// 用于统计测试时间
	_wfopen_s(&m_log_performance, (m_log_path + L"\\perf.csv").c_str(), L"w+");
	if (!m_log_performance) THROW_ERROR(ERR_USER, L"failed on opening log file %s\\perf.csv", m_log_path.c_str());
	fprintf_s(m_log_performance, "first_op,duration(us),op_num,avg_run(us)\n");
	m_op_sn = 0;
	return 0;
}

int CExTester::PreTest(void)
{
	// 准备测试
	CFsState* init_state = m_states.get();
	IFsSimulator* fs = nullptr;
	m_fs_factory->Clone(fs);
	init_state->Initialize("\\", fs);

	m_open_list.push_front(init_state);

	// 获取文件系统参数
	FS_INFO fs_info;
	m_fs_factory->GetFsInfo(fs_info);

	if (m_thread_num > 1)
	{

#ifdef THREAD_QUEUE
		InitializeCriticalSection(&m_sub_crit);
		InitializeCriticalSection(&m_cmp_crit);
		m_sub_doorbell = CreateSemaphore(nullptr, 0, MAX_WORK_NR, nullptr);
		m_cmp_doorbell = CreateSemaphore(nullptr, 0, MAX_WORK_NR, nullptr);
		m_thread_list = new HANDLE[m_thread_num];
#elif defined THREAD_POOL
		// 准备线程池
		//CreateThreadpoolWork()
		InitializeThreadpoolEnvironment(&m_tp_environ);
#endif

		for (int ii = 0; ii < MAX_WORK_NR; ++ii)
		{
			m_works[ii].tester = this;
			m_works[ii].state = nullptr;
			m_works[ii].result = ERR_UNKNOWN;
#ifdef THREAD_POOL
			m_works[ii].hevent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			m_work_events[ii] = m_works[ii].hevent;
			m_works[ii].work_item = CreateThreadpoolWork(FsOperator_Pool, m_works + ii, &m_tp_environ);
			LOG_DEBUG(L"creat work, no=%d, context=%p, work=%p", ii, m_works + ii, m_works[ii].work_item);
			if (m_works[ii].work_item == nullptr) THROW_WIN32_ERROR(L"failed on creating workor");
#elif defined THREAD_MULTI
			m_works[ii].hstart = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			m_works[ii].hthread = CreateThread(nullptr, 0, FsOperator_Thread, m_works + ii, 0, &m_works[ii].tid);
			if (m_works[ii].hthread == nullptr) THROW_WIN32_ERROR(L"failed on creating thread");
#endif
		}

#ifdef THREAD_QUEUE
		for (UINT ii = 0; ii < m_thread_num; ++ii)
		{
			m_thread_list[ii] = CreateThread(nullptr, 0, _FsOperator_Queue, this, 0, nullptr);
			if (m_thread_list[ii] == nullptr) THROW_WIN32_ERROR(L"faild on creating working thread %d", ii);
		}
#endif
		InitializeCriticalSection(&m_trace_crit);
	}
	srand(GetCurrentThreadId());

	memset(m_error_list, 0xFF, sizeof(m_error_list));
	return 0;
}

void CExTester::FinishTest(void)
{
	auto it = m_open_list.begin();
	for (; it != m_open_list.end(); ++it)
	{
		CFsState* state = *it;
		m_states.put(state);
	}
	m_open_list.clear();
	//停止线程

	if (m_thread_num > 1)
	{
#ifdef THREAD_QUEUE
		CloseHandle(m_sub_doorbell);
		CloseHandle(m_cmp_doorbell);
		DeleteCriticalSection(&m_sub_crit);
		DeleteCriticalSection(&m_cmp_crit);
		for (UINT ii = 0; ii < m_thread_num; ++ii)
		{
			CloseHandle(m_thread_list[ii]);
		}
		delete[] m_thread_list;
#elif defined THREAD_POOL
		for (int ii = 0; ii < MAX_WORK_NR; ii++)
		{
			CloseThreadpoolWork(m_works[ii].work_item);
			CloseHandle(m_works[ii].hevent);
			//		CloseHandle(m_works[ii].hstart);
		}
		DestroyThreadpoolEnvironment(&m_tp_environ);
#endif
		DeleteCriticalSection(&m_trace_crit);
	}

	LOG_DEBUG(L"max work num=%d", m_max_work);
	wprintf_s(L"max work num=%d\n", m_max_work);
}

//void ListIn(std::vector<TRACE_ENTRY>& ops, TRACE_ENTRY & op)

TRACE_ENTRY* op_index(std::vector<TRACE_ENTRY>& ops, size_t & index)
{
	TRACE_ENTRY* next = nullptr;
	if (index >= ops.size()) {
		ops.emplace_back();
		next = &ops.back();
	}
	else {
		next = &ops[index];
	}
	index++;
	return next;
}

void AddOp(TRACE_ENTRY* ops, size_t op_size, size_t& index, OP_CODE op_code, const std::string& file_path)
{
//	TRACE_ENTRY* op = op_index(ops, index);
	if (index >= op_size)	THROW_ERROR(ERR_APP, L"operation list overflow, size=%d", op_size);
	TRACE_ENTRY* op = ops + index;
	index++;

	op->op_code = op_code;
	op->file_path = file_path;
	op->offset = 0;
	op->length = 0;
	op->fid = 0;
}

void AddOp(TRACE_ENTRY* ops, size_t op_size, size_t& index, OP_CODE op_code, const std::string& file_path,
		UINT fid, FSIZE offset, FSIZE length)
{
//	TRACE_ENTRY* op = op_index(ops, index);
	if (index >= op_size)	THROW_ERROR(ERR_APP, L"operation list overflow, size=%d", op_size);
	TRACE_ENTRY* op = ops + index;
	index++;

	op->op_code = op_code;
	op->file_path = file_path;
	op->offset = offset;
	op->length = length;
	op->fid = fid;
}

void AddOp(TRACE_ENTRY* ops, size_t op_size, size_t& index, OP_CODE op_code)
{
//	TRACE_ENTRY* op = op_index(ops, index);
	if (index >= op_size)	THROW_ERROR(ERR_APP, L"operation list overflow, size=%d", op_size);
	TRACE_ENTRY* op = ops + index;
	index++;

	op->op_code = op_code;
	op->offset = 0;
	op->length = 0;
	op->fid = 0;
}

void AddOp(TRACE_ENTRY* ops, size_t op_size, size_t& index, OP_CODE op_code, UINT rollback)
{
//	TRACE_ENTRY* op = op_index(ops, index);
	if (index >= op_size)	THROW_ERROR(ERR_APP, L"operation list overflow, size=%d", op_size);
	TRACE_ENTRY* op = ops + index;
	index++;

	op->op_code = op_code;
//	op->offset = 0;
//	op->length = 0;
	op->fid = 0;
	op->rollback = rollback;
}

size_t CExTester::GenerateOps(CFsState* cur_state, TRACE_ENTRY* ops, size_t op_size)
{
	size_t index = 0;
	UINT first_op = m_op_sn;

	CReferenceFs& ref_fs = cur_state->m_ref_fs;
	auto endit = ref_fs.End();
	auto it = ref_fs.Begin();
	TRACE_ENTRY* op = nullptr;
	size_t file_nr = ref_fs.GetFileNumber();

	// 生成所有可能的操作，放入ops数组
	for (; it != endit; it++)
	{
//		if (context_id >= MAX_WORK_NR) THROW_ERROR(ERR_APP, L"too many operations");
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
			if (file_nr >= MAX_FILE_NUM) continue;

			AddOp(ops, op_size, index, OP_FILE_CREATE, path);
			AddOp(ops, op_size, index, OP_DIR_CREATE, path);
			if (child_num == 0 && path != "\\") {
				AddOp(ops, op_size, index, OP_DIR_DELETE, path);
			}
		}
		else
		{	// 文件
			FSIZE offset = (FSIZE)(file_len * ((float)(rand()) / RAND_MAX));
			FSIZE len = (FSIZE)((m_max_file_size - offset) * ((float)(rand()) / RAND_MAX));
			if (file.is_open())
			{
				if (file.write_count() < m_max_file_op)
				{
					AddOp(ops, op_size, index, OP_FILE_WRITE, path, file.get_fid(), 0, file_len);
				}
				AddOp(ops, op_size, index, OP_FILE_CLOSE, path, file.get_fid(), 0, 0);
			}
			else
			{
				AddOp(ops, op_size, index, OP_FILE_DELETE, path);
				AddOp(ops, op_size, index, OP_FILE_OPEN, path);
			}
		}
	}

	AddOp(ops, op_size, index, OP_DEMOUNT_MOUNT);
	// 生成对power off测试的op
	if (m_check_power_loss)
	{
		IFsSimulator* real_fs = cur_state->m_real_fs;
		UINT io_nr = real_fs->GetCacheNum();
		for (UINT ii = 1; ii < io_nr; ++ii)
		{
			AddOp(ops, op_size, index, OP_POWER_OFF_RECOVER, ii);
		}
	}
	return index;
}



ERROR_CODE CExTester::RunTest(void)
{
	ERROR_CODE ir = ERR_OK;
	printf_s("Running EXHAUSTICE test\n");
#if 0
	// 简化测试流程
	m_cur_state = m_open_list.front();
	IFsSimulator* fs = m_cur_state->m_real_fs;

//	RunTrace(fs, "T0722173846\\trace.json");
//	RunTrace(fs, "T0723232749\\trace.json");
//	RunTrace(fs, "T0724211127\\trace.json");
//	RunTrace(fs, "T0725000909\\trace.json");
//	RunTrace(fs, "T0726181132\\trace.json");
//	RunTrace(fs, "T0801152958\\trace.json");
//	RunTrace(fs, "T0802001039\\trace.json");
//	ir = RunTrace(fs, "T0806005205\\trace.json");
//	ir = RunTrace(fs, "T0809162728\\trace.json");
//	ir = RunTrace(fs, "T0809234843\\trace_15500.json");
	ir = RunTrace(fs, "T0810222823\\trace_28372.json");
#else
//	std::vector<TRACE_ENTRY> ops;
	TRACE_ENTRY ops[MAX_WORK_NR];
	while (!m_open_list.empty())
	{
		CFsState * cur_state = m_open_list.front();
		m_open_list.pop_front();
		// 如果verify出现错误，cur_state会在FinishTest()中被回收
		// (1) 将当前状态加入hash，仅加入hash，不保存状态
//		m_closed.Insert(cur_state);

		// check depth
		LONG depth = cur_state->m_depth;
		LONG max_depth = m_max_depth;
		if (depth > max_depth) {
			InterlockedExchange(&m_max_depth, depth);
			if (m_max_depth_state) m_states.put(m_max_depth_state);
			m_max_depth_state = cur_state;
			cur_state->add_ref();
		}
		// 用户控制停止
		if (m_stop_test > 0)
		{
			m_states.put(cur_state);		// put()中将m_cur_state置 nullptr
			wprintf_s(L"test stopped by user pressing ctrl C\n");
			break;
		}		
		
		if (depth >= m_test_depth)
		{
			m_states.put(cur_state);		// put()中将m_cur_state置 nullptr
			continue;
		}
		// (2) 扩展当前状态，
		auto it = m_open_list.begin();
		if (m_thread_num > 1)		ir = EnumerateOp_Thread_V2(ops, MAX_WORK_NR, cur_state, it);
		else						ir = EnumerateOpV2(ops, MAX_WORK_NR, cur_state, it);
		// (3) 移除当前状态      
		// 清除父节点
		m_states.put(cur_state);		// put()中将m_cur_state置 nullptr
//		if (ir != ERR_OK) break;
	}
#endif
	if (ir != ERR_OK)
	{
		wprintf_s(L"test failed with error code=%d\n", ir);
	}
	if (m_max_depth_state)
	{
		OutputTrace_Thread(m_max_depth_state, ERR_OK, "max_depth");
		m_states.put(m_max_depth_state);
	}
	return ir;
}

int CExTester::StartTest(void)
{
	// 主线程用于监视，子线程用于测试
	m_ts_start = boost::posix_time::microsec_clock::local_time();
	// 启动测试
	m_test_thread = CreateThread(NULL, 0, _RunTest, (PVOID)this, 0, &m_test_thread_id);

	// 监视测试过程
	while (1)
	{
		DWORD ir = WaitForSingleObject(m_test_thread, m_update_ms);
		boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
		INT64 ts = (ts_cur - m_ts_start).total_seconds();
		PrintProgress(ts);

		if (ir == 0)
		{	// test completed
			wprintf_s(L"test completed, id=%s\n", m_log_path.c_str());
			break;
		}
	}
	if(m_log_file) fclose(m_log_file);
	fclose(m_log_performance);
	return 0;
}

DWORD WINAPI CExTester::_RunTest(PVOID p)
{
	CExTester* tester = (CExTester*)p;
	int ir = ERR_GENERAL;
	try
	{
		tester->PreTest();
		ir = tester->RunTest();
		wprintf_s(L"test completed with code=%d\n", ir);
	}
	catch (jcvos::CJCException & err)
	{
		ir = ERR_GENERAL;
		if (tester->m_log_file) {
			fprintf_s(tester->m_log_file, "[err] test failed with exception (code=%d): %S\n", ir, err.WhatT());
		}
		printf_s("[err] test failed with exception (code=%d): %S\n", ir, err.WhatT());
	}
	catch (const std::exception& err)
	{
		ir = ERR_GENERAL;
		wprintf_s(L"test failed with error code=%d, msg=%S\n", ir, err.what());
	}

	tester->FinishTest();
	return ir;
}

bool CExTester::PrintProgress(INT64 ts)
{
	bool health_valid = false;
	HANDLE handle = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS_EX pmc = { 0 };
	GetProcessMemoryInfo(handle, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
	float mem_cost = (float)(pmc.WorkingSetSize / (1024.0 * 1024.0));	//MB
	if (mem_cost > m_max_memory_cost) m_max_memory_cost = mem_cost;

	// 输出到concole
	wprintf_s(L"ts=%llds, op=%d, depth=%d, closed=%zd, open=%zd, mem=%.1fMB, max_mem=%.1fMB\n"
		L"files=%d, logic=%d, phisic=%d, total=%d, free=%d, host write=%lld, media write= %lld\n",
		ts, m_op_sn, m_max_depth, m_closed.size(), m_open_list.size(), mem_cost, m_max_memory_cost,
		m_file_num, m_logical_blks, m_physical_blks, m_total_blks, m_free_blks,m_host_write, m_media_write);
	return true;
}

void CExTester::ShowTestFailure(FILE* log)
{
}

void CExTester::GetTestSummary(boost::property_tree::wptree& pt_sum)
{
	boost::property_tree::wptree sum;
	sum.add(L"op_num", m_op_sn);

	boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
	INT64 ts = (ts_cur - m_ts_start).total_seconds();
	sum.add(L"duration", ts);			//s
	sum.add(L"max_memory", m_max_memory_cost);		//MB
	size_t state = m_closed.size();
	sum.add(L"state_closed", state);
	sum.add(L"max_depth", m_max_depth);
	sum.add(L"max_file_num", m_file_num);
	sum.add(L"max_logical_blks", m_logical_blks);
	sum.add(L"max_physical_blks", m_physical_blks);
	sum.add(L"min_free_blks", m_free_blks);
	sum.add(L"max_host_write", m_host_write);
	sum.add(L"max_media_write", m_media_write);
	sum.add(L"total_blks", m_total_blks);
	sum.add(L"thread_num", m_thread_num);
	sum.add(L"branch", m_branch);
	pt_sum.add_child(L"exhaustive_summary", sum);
}

void CExTester::StopTest(void)
{
	LOG_DEBUG(L"Stop Test");
	wprintf_s(L"Ctrl +C pressed\n");
	InterlockedIncrement(&m_stop_test);
	DWORD ir = WaitForSingleObject(m_test_thread, INFINITE);
}



ERROR_CODE CExTester::EnumerateOpV2(TRACE_ENTRY* ops, size_t op_size, CFsState* cur_state, std::list<CFsState*>::iterator& insert)
{
	LOG_STACK_TRACE();
	size_t op_nr = GenerateOps(cur_state, ops, op_size);
	for (size_t ii = 0; ii < op_nr; ii++)
	{
		ERROR_CODE ir = DoFsOperator(cur_state, ops[ii], insert);
		if (ir != ERR_OK) return ir;
	}

#if 0
	int context_id = 0;
	UINT first_op = m_op_sn;
	CReferenceFs& ref_fs = cur_state->m_ref_fs;
	auto endit = ref_fs.End();
	auto it = ref_fs.Begin();
	TRACE_ENTRY op;
	ERROR_CODE ir = ERR_OK; // 记录操作的结果

	for (; it != endit; it++)
	{
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
			// 子文件，目录数量限制
			if (child_num >= m_max_child_num) continue;
			// 文件深度限制
			if (file.depth() >= (m_max_dir_depth - 1)) continue;
			// 文件总数限制
			if (ref_fs.GetFileNumber() >= MAX_FILE_NUM) continue;

			// create a sub file
			char fn[3];	//文件名，随机产生2字符
			GenerateFn(fn, 2);
			op.op_code = OP_FILE_CREATE;
			op.file_path = (path.size() > 1) ? (path + "\\" + fn) : (path + fn);
			ir = DoFsOperator(cur_state, op, insert);
			if (ir != ERR_OK) return ir;
			context_id++;

			// dreate a sub dir		// 深度限制
			GenerateFn(fn, 2);
			op.op_code = OP_DIR_CREATE;
			op.file_path = (path.size() > 1) ? (path + "\\" + fn) : (path + fn);
			ir = DoFsOperator(cur_state, op, insert);
			if (ir != ERR_OK) return ir;
			context_id++;
			// <TODO> move dir
			// <TODO> delete dir
		}
		else
		{	// 文件
			FSIZE offset = (FSIZE)(file_len * ((float)(rand()) / RAND_MAX));
			FSIZE len = (FSIZE)((m_max_file_size - offset) * ((float)(rand()) / RAND_MAX));
			if (file.is_open() )
			{	// 文件已经打开，可以：写入，关闭，读取，删除
				op.fid = file.get_fid();
				// 写入
				if (file.write_count() < m_max_file_op)
				{
					op.op_code = OP_FILE_WRITE;
					op.file_path = path;
					op.offset = offset;
					op.length = len;
					ir = DoFsOperator(cur_state, op, insert);
					if (ir != ERR_OK) return ir;
					context_id++;
				}
				// 关闭
				op.op_code = OP_FILE_CLOSE;
				op.file_path = path;
				ir = DoFsOperator(cur_state, op, insert);
				if (ir != ERR_OK) return ir;
				context_id++;
				// <TODO> read file
				// <TODO> delete file
			}
			else
			{	// 文件没有打开，可以：打开，删除
				// delete
				op.op_code = OP_FILE_DELETE;
				op.file_path = path;
				ir = DoFsOperator(cur_state, op, insert);
				if (ir != ERR_OK) return ir;
				context_id++;

				// oepn file
				op.op_code = OP_FILE_OPEN;
				op.file_path = path;
				ir = DoFsOperator(cur_state, op, insert);
				if (ir != ERR_OK) return ir;
				context_id++;
			}
		}
	}
	op.op_code = OP_DEMOUNT_MOUNT;
	ir = DoFsOperator(cur_state, op, insert);
	if (ir != ERR_OK) return ir;
	context_id++;
#endif
	UpdateFsParam(cur_state->m_real_fs);
	return ERR_OK;
}

// for both single thread and multi thread
ERROR_CODE CExTester::FsOperatorCore(CFsState* state, TRACE_ENTRY& op)
{
	ERROR_CODE ir = ERR_OK;
	// (2) 在新的测试状态上执行测试
//	std::string &path =  op.file_path;
	bool is_power_test = false;
	switch (op.op_code)
	{
	case OP_FILE_CREATE: {
		char fn[MAX_FILENAME_LEN + 1];
		GenerateFn(fn, MAX_FILENAME_LEN);
		std::string path = (op.file_path.size() > 1) ? (op.file_path + "\\" + fn) : (op.file_path + fn);
		op.file_path = path;
		op.offset = 0;
		op.length = 0;
		ir = TestCreateFile(state, path);
		break; }
	case OP_DIR_CREATE: {
		char fn[MAX_FILENAME_LEN + 1];
		GenerateFn(fn, MAX_FILENAME_LEN);
		std::string path = (op.file_path.size() > 1) ? (op.file_path + "\\" + fn) : (op.file_path + fn);
		op.file_path = path;
		op.offset = 0;
		op.length = 0;
		ir = TestCreateDir(state, op.file_path);
		break; }

	case OP_FILE_WRITE: {
		FSIZE offset = (FSIZE)(op.length * ((float)(rand()) / RAND_MAX));
		FSIZE len = (FSIZE)((m_max_file_size - offset) * ((float)(rand()) / RAND_MAX));
		op.offset = offset;
		op.length = len;
		ir = TestWriteFileV2(state, op.fid, offset, len, op.file_path);
		break; }

	case OP_FILE_DELETE:
		op.offset = 0;
		op.length = 0;

		ir = TestDeleteFile(state, op.file_path);
		break;

	case OP_DIR_DELETE:
		op.offset = 0;
		op.length = 0;
		ir = TestDeleteDir(state, op.file_path);
		break;

	case OP_FILE_OPEN:
		op.offset = 0;
		op.length = 0;
		ir = TestOpenFile(state, op.file_path);
		break;

	case OP_FILE_CLOSE:
		op.offset = 0;
		op.length = 0;

		ir = TestCloseFile(state, op.fid, op.file_path);
		break;

		//	case OP_FILE_OVERWRITE:
	case OP_MOVE:
		break;
	case OP_DEMOUNT_MOUNT:
		op.offset = 0;
		op.length = 0;
		op.file_path = "";
		ir = TestMount(state);
		break;

	case OP_POWER_OFF_RECOVER:
		//op.offset = 0;
		//op.length = 0;
		op.file_path = "";
		ir = TestPowerOutage(state, op.rollback);
		is_power_test = true;
		break;
	}
	state->m_result = ir;
	// (3) 检查测试结果
	if (ir != ERR_OK && ir != ERR_NO_OPERATION)
	{
		THROW_FS_ERROR(ir, L"run test error op=%d, file=%S, ", ir, CFsException::ErrCodeToString(ir), op.op_code, op.file_path.c_str());
	}
//	if (is_power_test)	ir = VerifyForPower(state);
//	else				ir = Verify(state);
//	if (ir != ERR_OK) THROW_FS_ERROR(ir, L"verify failed, code=%d", ir);
	return ir;
}

ERROR_CODE CExTester::DoFsOperator(CFsState* cur_state, TRACE_ENTRY& op, std::list<CFsState*>::iterator& insert)
{
	LOG_STACK_PERFORM(L"fs_operation");
	UINT opsn = m_op_sn;
//	LOG_DEBUG(L"start work: op=%d, code=%d", opsn, op.op_code);

	JCASSERT(cur_state);
	// 标准测试流程：
	// (1) 复制测试状态，复制的状态，要么放入open list，在FinishTest中回收，要么在返回前回收。
	CFsState* new_state = m_states.duplicate(cur_state);
//	InterlockedIncrement(&(cur_state->m_ref));

	new_state->m_op = op;
	new_state->m_op.op_sn = m_op_sn++;
	ERROR_CODE ir = ERR_OK;

	try {
		ir = FsOperatorCore(new_state, new_state->m_op);
		if (m_closed.Check(new_state))
		{
			m_states.put(new_state);
			return ERR_OK;
		}
		if (new_state && ir== ERR_OK)
		{
			m_open_list.insert(insert, new_state);
			return ERR_OK;
		}
	}
	catch (jcvos::CJCException & err)
	{
		TEST_ERROR("test failed with exception: %S", err.WhatT());
		ir = ERR_GENERAL;
	}

	if (ir != ERR_OK)
	{
		OutputTrace(new_state);
	}
	m_states.put(new_state);
	return ir;
}

template <typename T>
void max_update(T& v, T a)
{
	//if (a > v) InterlockedExchange(&v, a);
	if (a > v) v = a;
}

//template <LONG64>
void max_update64(LONG64& v, LONG64 a)
{
	if (a > v) InterlockedExchange64(&v, a);

}

template <typename T>
void min_update(T& v, T a)
{
//	if (a < v) InterlockedExchange(&v, a);
	if (a < v) v = a;
}

void CExTester::UpdateFsParam(IFsSimulator* fs)
{
	FS_INFO src_info;
	fs->GetFsInfo(src_info);
	FsHealthInfo health_info;
	fs->GetHealthInfo(health_info);

	max_update(m_logical_blks, src_info.used_blks);
	m_total_blks = src_info.total_blks;
	UINT ph_blks = src_info.total_blks - src_info.free_blks;
	max_update(m_physical_blks, src_info.physical_blks);
	max_update64(m_host_write, health_info.m_total_host_write/* src_info.total_host_write*/);
	max_update64(m_media_write, health_info.m_total_media_write /*src_info.total_media_write*/);
	min_update(m_free_blks, src_info.free_blks);
}

ERROR_CODE CExTester::Verify(CFsState* state)
{
	IFsSimulator* real_fs = state->m_real_fs;
	CReferenceFs& ref_fs = state->m_ref_fs;
	return VerifyState(ref_fs, real_fs);
}

ERROR_CODE CExTester::VerifyForPower(CFsState* cur_state)
{
	CFsState* state = cur_state;
	IFsSimulator* real_fs = state->m_real_fs;
	ERROR_CODE err = ERR_OK;
	while (state != nullptr)
	{
		CReferenceFs& ref_fs = state->m_ref_fs;
		err = VerifyState(ref_fs, real_fs);
		if ((err == ERR_OK) || (err == ERR_NO_OPERATION))
		{
			if (state != cur_state)
			{	// 用正确的状态覆盖原来状态
				cur_state->m_ref_fs.CopyFrom(state->m_ref_fs);
			}
			break;
		}
		// 对于power off测试，回溯到前一个稳定状态。
		if (state->m_stable == true) break;
		state = state->m_parent;
	}
	// 关闭所有文件，
	cur_state->m_ref_fs.Demount();
	return err;
}

ERROR_CODE CExTester::VerifyState(CReferenceFs & ref_fs, IFsSimulator * real_fs)
{
	LOG_STACK_PERFORM(L"verification");
	ERROR_CODE ir = ERR_OK;
	TEST_LOG("[BEGIN VERIFY]\n");
	size_t checked_file = 0, checked_total = 0;

	// 获取文件系统信息：文件、目录总数；文件数量；逻辑饱和度；物理饱和度；空闲块；host write; media write
	try {
		FS_INFO fs_info;
		real_fs->GetFsInfo(fs_info);
		UINT ph_blks = fs_info.total_blks - fs_info.free_blks;

		UINT ref_dir_nr = ref_fs.m_dir_num, ref_file_nr = ref_fs.m_file_num;
		UINT real_file_nr = 0, real_dir_nr = 0;
		real_fs->GetFileDirNum(0, real_file_nr, real_dir_nr);		// TODO: os需要保存file number以供检查

		if (real_dir_nr != ref_dir_nr) {
			THROW_FS_ERROR(ERR_WRONG_FILE_NUM, L"directory number does not match, ref:%d, fs:%d",
				ref_dir_nr, real_dir_nr);
		}
		if (real_file_nr != ref_file_nr) {
			THROW_FS_ERROR(ERR_WRONG_FILE_NUM, L"file number does not match, ref:%d, fs:%d", 
				ref_file_nr, real_file_nr);
		}
		FSIZE total_file_blks = 0;

		char str_encode[MAX_ENCODE_SIZE];
		ref_fs.Encode(str_encode, MAX_ENCODE_SIZE);

		auto endit = ref_fs.End();
		for (auto it = ref_fs.Begin(); it != endit; ++it)
		{
			const CReferenceFs::CRefFile& ref_file = ref_fs.GetFile(it);
			std::string path;
			ref_fs.GetFilePath(ref_file, path);
			if (path == "\\") continue;	//不对根目录做比较

			checked_total++;
			bool dir = ref_fs.IsDir(ref_file);
			ref_fs.Encode(str_encode, MAX_ENCODE_SIZE);
			DWORD access = 0;
			DWORD flag = 0;
			if (dir) { flag |= FILE_FLAG_BACKUP_SEMANTICS; }
			else { access |= GENERIC_READ; }

			TEST_LOG("  check %s: %s\n", dir ? "DIR " : "FILE", path.c_str());
//			if (dir) continue;
			NID fid = INVALID_BLK;
			ir = real_fs->FileOpen(fid, path);
			if (ir == ERR_MAX_OPEN_FILE)
			{
				ir = ERR_OK;
				continue;		// 打开文件已经超过上限
			}

			if (ir != ERR_OK || is_invalid(fid) )
			{
				THROW_FS_ERROR(ir == ERR_OK ? ERR_OPEN_FILE : ir, L"failed on open file=%S", path.c_str());
			}
			checked_file++;
			if (dir) {
				real_fs->FileClose(fid);
				continue;
			}
			DWORD ref_checksum;
			FSIZE ref_len, file_secs = real_fs->GetFileSize(fid);
			ref_fs.GetFileInfo(ref_file, ref_checksum, ref_len);
			total_file_blks += ref_len;

			if (ref_len != file_secs)
			{
				ir = ERR_WRONG_FILE_SIZE;
				TEST_ERROR("file length does not match ref=%d, file=%d", ref_len, file_secs);
				if (is_valid(fid) ) real_fs->FileClose(fid);
				THROW_FS_ERROR(ERR_VERIFY_FILE, L"file length does not match ref=%d, file=%d", ref_len, file_secs);
			}
			FILE_DATA data[MAX_FILE_BLKS];
			size_t page_nr = real_fs->FileRead(data, fid, 0, file_secs);
			FSIZE ss = 0;

			int compare = 0;
			for (size_t ii = 0; ii < page_nr; ++ii, ss++)
			{
				if (data[ii].fid != fid || data[ii].offset != ss) {			
					if (is_invalid(fid)) real_fs->FileClose(fid);
					TEST_ERROR("file data mismatch, fid=%d, lblk=%d, page_fid=%d, page_offset=%d", fid, ss, data[ii].fid, data[ii].offset);
					compare++;
				}
			}
			if (compare > 0) {
				if (is_valid(fid)) real_fs->FileClose(fid);
				THROW_FS_ERROR(ERR_VERIFY_FILE, L"read file mismatch, %d secs", compare);
			}
			real_fs->FileClose(fid);
		}
	}
	catch (jcvos::CJCException & err)
	{
		ir = (ERROR_CODE)(err.GetErrorCode());
		if (ir > ERR_UNKNOWN) ir = ERR_UNKNOWN;
		ir = ir;
	}
	TEST_LOG("[END VERIFY], total checked=%zd, file checked= %zd\n", checked_total, checked_file);
	return ir;
}

ERROR_CODE CExTester::TestCreateFile(CFsState* cur_state, const std::string& path)
{
	if (path.size() >= MAX_PATH_SIZE) return ERR_NO_OPERATION;

	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE ir = ERR_OK;

	// 检查自己点数量是否超标
	NID fid = INVALID_BLK;
	try {
		ir = real_fs->FileCreate(fid, path);
	}
	catch (jcvos::CJCException& err)	{
		CFsException* _err = dynamic_cast<CFsException*>(&err);
		if (_err) ir = _err->get_error_code();
		else ir = ERR_UNKNOWN;
	}
	if (ir != ERR_OK) {
		LOG_ERROR(L"[ir] failed on creating file %S, err(%d):%s", path.c_str(), ir, CFsException::ErrCodeToString(ir));
	}

	if (ir == ERR_MAX_OPEN_FILE || ir == ERR_NO_SPACE )
	{	// 达到文件系统极限
		return ERR_NO_OPERATION;
	}

	// check if doubled name, update ref fs
	if (ref.IsExist(path))
	{	// 文件已经存在，要求返回false
		TEST_LOG("[OPERATE ](%d) CREATE FILE, path=%s, existing file\n", cur_state->m_op.op_sn, path.c_str());
		if (is_valid(fid) ) { ir = ERR_CREATE_EXIST; }		// 不应该创建成功
		else { ir = ERR_NO_OPERATION; }						// 创建失败。测试成功，返回no operation.
	}
	else
	{	// create file in fs
		TEST_LOG("[OPERATE ](%d) CREATE FILE, path=%s, new file\n", cur_state->m_op.op_sn, path.c_str());
		CReferenceFs::CRefFile * ref_file = ref.AddPath(path, false, fid);
		if (is_invalid(fid) )
		{
			if (ir == ERR_OK) 	ir = ERR_CREATE;
			return ir;
		}
		ref.OpenFile(*ref_file);
	}
	return ir;
}

ERROR_CODE CExTester::TestDeleteFile(CFsState* cur_state, const std::string& path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE ir = ERR_OK;
	TEST_LOG("[OPERATE ](%d) DELETE FILE, path=%s,", cur_state->m_op.op_sn, path.c_str());

	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (ref_file == nullptr) THROW_ERROR(ERR_APP, L"file %S not in ref fs", path.c_str());
	try {
		real_fs->FileDelete(path);
	}
	catch (jcvos::CJCException& err) {
		CFsException* _err = dynamic_cast<CFsException*>(&err);
		if (_err) ir = _err->get_error_code();
		else ir = ERR_UNKNOWN;
	}
	if (ir==ERR_OK) ref.RemoveFile(path);
	TEST_CLOSE_LOG;
	return ir;
}

ERROR_CODE CExTester::TestDeleteDir(CFsState* cur_state, const std::string& path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE ir = ERR_OK;
	TEST_LOG("[OPERATE ](%d) DELETE DIR , path=%s,", cur_state->m_op.op_sn, path.c_str());

	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (ref_file == nullptr) THROW_ERROR(ERR_APP, L"file %S not in ref fs", path.c_str());

	try {
		ir = real_fs->DirDelete(path);
	}
	catch (jcvos::CJCException& err) {
		CFsException* _err = dynamic_cast<CFsException*>(&err);
		if (_err) ir = _err->get_error_code();
		else ir = ERR_UNKNOWN;
	}
	if (ir == ERR_OK)
	{
		ref.RemoveFile(path);
	}
	//	if (ref_file->is_open()) real_fs->FileClose(ref_file->get_fid());
	TEST_CLOSE_LOG;
	return ir;
}

ERROR_CODE CExTester::TestMount(CFsState* cur_state)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs & ref = cur_state->m_ref_fs;
	ref.Demount();

	ERROR_CODE ir = ERR_OK;
	try {
		real_fs->Unmount();
		real_fs->Reset(0);
		real_fs->Mount();
		TEST_LOG("[OPERATE ](%d) DEMOUNT MOUNT\n", cur_state->m_op.op_sn);
		ir = Verify(cur_state);
	}
	catch (jcvos::CJCException & err)
	{
		CFsException* e = dynamic_cast<CFsException*>(&err);
		if (e) ir = e->get_error_code();
		else ir = ERR_UNKNOWN;
		LOG_ERROR(L"[err] test fail during mouting, code=%d, msg=%s", ir, err.WhatT());
	}

	cur_state->m_stable = true;
	return ir;
}

ERROR_CODE CExTester::TestPowerOutage(CFsState* cur_state, UINT rollback)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;
//	ref.Demount();

	// 获取fs的io list

	// 
	// 对每个尝试每个io, 并且检查完整性

	// 选择一个io, mount，做下一步测试
	cur_state->m_stable = false;
	real_fs->Reset(rollback);
	real_fs->fsck(true);
	real_fs->Mount();
	TEST_LOG("[OPERATE ](%d) POWER_OUTAGE\n", cur_state->m_op.op_sn);

	ERROR_CODE ir = VerifyForPower(cur_state);
	cur_state->m_stable = true;
	return ir;
	//if (ir != ERR_OK) return ir;
	//return ERR_OK;
}

ERROR_CODE CExTester::TestCreateDir(CFsState* cur_state, const std::string& path)
{
	if (path.size() >= MAX_PATH_SIZE) return ERR_NO_OPERATION;

	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE ir = ERR_OK;
	TEST_LOG("[OPERATE ](%d) CREATE DIR_, path=%s,", cur_state->m_op.op_sn, path.c_str());

	//bool create_result = false;
	NID fid = INVALID_BLK;
	try {
		ir = real_fs->DirCreate(fid, path);
	}
	catch (jcvos::CJCException& err) {
		CFsException* _err = dynamic_cast<CFsException*>(&err);
		if (_err) ir = _err->get_error_code();
		else ir = ERR_UNKNOWN;
	}
	if (ir == ERR_MAX_OPEN_FILE || ir == ERR_NO_SPACE) {
		return ERR_NO_OPERATION;
	}

	// check if doubled name, update ref fs
	if (ref.IsExist(path))
	{	// 文件已经存在，要求返回false
//		TEST_LOG(" existing dir");
		if (is_valid(fid))
		{
			ir = ERR_CREATE_EXIST;
			TEST_ERROR("create a file which is existed path=%s.", path.c_str());
		}
		else ir = ERR_OK;
//		else	ir = ERR_OK;
	}
	else
	{	// create file in fs
		if (is_valid(fid))
		{
			ref.AddPath(path, true, fid);
			ir = ERR_OK;
		}
		//if (is_invalid(fid) )
		//{
		//	ir = ERR_CREATE;
		//	TEST_ERROR("failed on creating file fn=%s", path.c_str());
		//}
//		ir = ERR_OK;
	}
//	TEST_CLOSE_LOG;
	return ir;
}


ERROR_CODE CExTester::TestWriteFileV2(CFsState* cur_state, NID fid, FSIZE offset, FSIZE len, const std::string & path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;

	//随机生成读写长度和偏移
	//	len &= ~3;		// DWORD对齐
	TEST_LOG("[OPERATE ](%d) WriteFile, path=%s, offset=%d, size=%d\n", cur_state->m_op.op_sn, path.c_str(), offset, len);

	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (!ref_file) THROW_ERROR(ERR_USER, L"cannof find ref file: %s", path.c_str());
	if (ref_file->get_fid() != fid) {
		THROW_ERROR(ERR_APP, L"file id does not match, file=%S, real fid=%d, ref fid=%d",
			path.c_str(), fid, ref_file->get_fid());
	}

	if (!ref_file->is_open() ) {
		THROW_ERROR(ERR_APP, L"file is not opened in ref, file=%S, fid=%d", path.c_str(), fid);
	}

	FSIZE cur_ref_size;
	DWORD cur_checksum;
	ref.GetFileInfo(*ref_file, cur_checksum, cur_ref_size);
	//FS_INFO space_info;
	//real_fs->GetFsInfo(space_info);

	// get current file length
	FSIZE cur_file_size = real_fs->GetFileSize(fid);
	if (cur_ref_size != cur_file_size) {
		THROW_ERROR(ERR_USER, L"file length does not match ref=%d, file=%d", cur_ref_size, cur_file_size);
	}

	FSIZE written = real_fs->FileWrite(fid, offset, len);
	FSIZE end_pos = offset + written;

	if (end_pos > cur_file_size) cur_file_size = end_pos;

	DWORD checksum = fid;
	ref.UpdateFile(path, checksum, cur_file_size);
	return err;
}

ERROR_CODE CExTester::TestOpenFile(CFsState* cur_state, const std::string& path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;

	//if (ref.OpenedFileNr() + 2 >= m_max_opened_file_nr) {
	//	TEST_LOG("[OPERATE ](%d) OPEN FILE, path=%s, max opened file reached\n", cur_state->m_op.op_sn, path.c_str());
	//	return err;
	//}

	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (!ref_file) THROW_ERROR(ERR_APP, L"cannof find ref file: %S", path.c_str());

	NID fid = INVALID_BLK;
	err = real_fs->FileOpen(fid, path);
	if (err == ERR_MAX_OPEN_FILE) return ERR_OK;

	ref.OpenFile(*ref_file);
	if (err != ERR_OK || is_invalid(fid))
	{
//		err = ERR_OPEN_FILE;
		THROW_FS_ERROR(err == ERR_OK ? ERR_OPEN_FILE : err, L"failed on opening file, fn=%S", path.c_str());
	}
	if (fid != ref_file->get_fid()) {
		THROW_ERROR(ERR_USER, L"file id does not match, file=%S, real fid=%d, ref fid=%d", 
			path.c_str(), fid, ref_file->get_fid());
	}
	//ref_file->m_is_open = true;

	TEST_LOG("[OPERATE ](%d) OPEN FILE, path=%s, fid=%d\n", cur_state->m_op.op_sn, path.c_str(), fid);
	return err;
}

ERROR_CODE CExTester::TestCloseFile(CFsState* cur_state, NID fid, const std::string& path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;
	ERROR_CODE ir = ERR_OK;

	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (!ref_file) THROW_ERROR(ERR_APP, L"cannof find ref file: %S", path.c_str());
	if (ref_file->get_fid() != fid) {
		THROW_ERROR(ERR_APP, L"file id does not match, file=%S, reaf fid=%d, ref fid=%d",
			path.c_str(), fid, ref_file->get_fid());
	}
	try {
		real_fs->FileClose(fid);
	}
	catch (jcvos::CJCException& err) {
		CFsException* _err = dynamic_cast<CFsException*>(&err);
		if (_err) ir = _err->get_error_code();
		else ir = ERR_UNKNOWN;
	}
	if (ir== ERR_OK)	ref.CloseFile(*ref_file);

	TEST_LOG("[OPERATE ](%d) CLOSE FILE, path=%s, fid=%d\n", cur_state->m_op.op_sn, path.c_str(), fid)
	return ir;
}
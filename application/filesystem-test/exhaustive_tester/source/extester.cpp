///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"
#include <Psapi.h>
#include <boost/property_tree/json_parser.hpp>

LOCAL_LOGGER_ENABLE(L"extester", LOGGER_LEVEL_DEBUGINFO);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State ==

CFsState::~CFsState(void)
{
	delete m_real_fs;
}

void CFsState::OutputState(FILE* log_file)
{
	// output ref fs
	JCASSERT(log_file);

	// 检查Encode
	ENCODE encode;
	int len = m_ref_fs.Encode(encode.code);
	char* str_encode = (char*)(encode.code);

	fprintf_s(log_file, "[REF FS]: encode={%s}\n", str_encode);
	auto endit = m_ref_fs.End();
	auto it = m_ref_fs.Begin();
	for (; it != endit; it++)
	{
		const CReferenceFs::CRefFile& ref_file = m_ref_fs.GetFile(it);
		std::string path;
		m_ref_fs.GetFilePath(ref_file, path);
		bool dir = m_ref_fs.IsDir(ref_file);
		DWORD ref_checksum;
		FSIZE ref_len;
		m_ref_fs.GetFileInfo(ref_file, ref_checksum, ref_len);
		fprintf_s(log_file, "<%s> %s : ", dir ? "dir " : "file", path.c_str());
		if (dir)	fprintf_s(log_file, "children=%d\n", ref_checksum);
		else		fprintf_s(log_file, "size=%d, checksum=0x%08X\n", ref_len, ref_checksum);
	}
	// output op
	char str[256];
	Op2String(str, m_op);
	fprintf_s(log_file, "%s\n", str);
	fprintf_s(log_file, "[END of REF FS]\n");
}

void CFsState::DuplicateFrom(CFsState* src_state)
{
	LOG_STACK_PERFORM(L"state_duplication");
	m_ref_fs.CopyFrom(src_state->m_ref_fs);
	if (m_real_fs == nullptr) 	src_state->m_real_fs->Clone(m_real_fs);
	else m_real_fs->CopyFrom(src_state->m_real_fs);
	m_depth = src_state->m_depth + 1;
	m_parent = src_state;
	m_ref = 1;
//	InterlockedIncrement(&(src_state->m_ref));
//	src_state->m_ref++;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State Manager ==
CStateManager::CStateManager(void)
{
}

CStateManager::~CStateManager(void)
{	// 清空free
	while (m_free_list)
	{
		CFsState* next = m_free_list->m_parent;
		delete m_free_list;
		m_free_list = next;
		m_free_nr--;
	}
	LOG_DEBUG(L"free = %zd", m_free_nr);
}

void CStateManager::Initialize(size_t size)
{
}

CFsState* CStateManager::get(void)
{
	CFsState* new_state = nullptr;
	if (m_free_nr > 0)
	{
		new_state = m_free_list;
		m_free_list = new_state->m_parent;
		new_state->m_parent = nullptr;
		m_free_nr--;
	}
	else
	{
		new_state = new CFsState;
	}
	return new_state;
}

void CStateManager::put(CFsState* &state)
{
	while (state)
	{
		UINT ref = InterlockedDecrement(&state->m_ref);
		if (ref != 0) break;
		CFsState* pp = state->m_parent;
//		delete state;
		// 放入free list
		state->m_parent = m_free_list;
		m_free_list = state;
		m_free_nr++;

		state = pp;
	}
	state = nullptr;
}

CFsState* CStateManager::duplicate(CFsState* state)
{
	CFsState* new_state = get();
//	LOG_DEBUG_(1, L"duplicate state: <%p>", new_state);
	new_state->DuplicateFrom(state);
	return new_state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State heap and hash code ==

bool CStateHeap::Check(const CFsState* state)
{
	const CReferenceFs& fs = state->m_ref_fs;
	ENCODE encode;
	int len = fs.Encode(encode.code);
	auto it = m_fs_state.find(encode);
	return it != m_fs_state.end();
}


void CStateHeap::Insert(const CFsState* state)
{
	const CReferenceFs& fs = state->m_ref_fs;
	ENCODE encode;
	int len = fs.Encode(encode.code);
	m_fs_state.insert(encode);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Help functions ==
void GenerateFn(char* fn, size_t len)
{
	size_t ii;
	for (ii = 0; ii < len; ++ii)
	{
		fn[ii] = rand() % 26 + 'A';
	}
	fn[ii] = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Exahustive Tester ==
CExTester::CExTester(void)
{
}

CExTester::~CExTester(void)
{
//	RELEASE(m_fs_factory);
}


int CExTester::PrepareTest(const boost::property_tree::wptree& config, IFsSimulator * fs, const std::wstring & log_path)
{
	JCASSERT(fs);
	m_fs_factory = fs;
	m_timeout = config.get<DWORD>(L"timeout", INFINITE);
	m_update_ms = config.get<DWORD>(L"message_interval", 30);		// 以秒为单位的更新时间
	m_format = config.get<bool>(L"format_before_test", false);
	m_mount = config.get<bool>(L"mount_before_test", false);
	m_test_depth = config.get<int>(L"depth", 5);
	m_max_child_num = config.get<size_t>(L"max_child", 5);
	m_max_dir_depth = config.get<size_t>(L"max_dir_depth", 5);
	m_max_file_op = config.get<size_t>(L"max_file_op", 5);
	m_thread_num = config.get<UINT>(L"thread_num", 8);

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
	m_log_file = _wfsopen(trace_log_fn.c_str(), L"w+", _SH_DENYNO);
	if (!m_log_file) THROW_ERROR(ERR_USER, L"failed on opening log file %s", log_path.c_str());

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
	init_state->m_ref = 1;
	m_open_list.push_front(init_state);

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
		m_works[ii].ir = ERR_UNKNOWN;
#ifdef THREAD_POOL
		m_works[ii].hevent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		m_work_events[ii] = m_works[ii].hevent;
		m_works[ii].work_item = CreateThreadpoolWork(FsOperator_Pool, m_works+ii, &m_tp_environ);
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

	return 0;
}

void CExTester::FinishTest(void)
{
	auto it = m_open_list.begin();
	for (; it != m_open_list.end(); ++it)
	{
		CFsState* state = *it;
		//state->m_ref--;
		//if (state->m_ref == 0) m_states.put(state);
		m_states.put(state);
	}
	m_open_list.clear();
	//停止线程

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

	LOG_DEBUG(L"max work num=%d", m_max_work);
	wprintf_s(L"max work num=%d\n", m_max_work);
}


int CExTester::RunTest(void)
{
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
	RunTrace(fs, "T0802001039\\trace.json");
#else
	while (!m_open_list.empty())
	{
		m_cur_state = m_open_list.front();
		// (1) 将当前状态加入hash，仅加入hash，不保存状态
		m_closed.Insert(m_cur_state);
		// 如果verify出现错误，cur_state会在FinishTest()中被回收
		m_open_list.pop_front();

		// check depth
		LONG depth = m_cur_state->m_depth;
		LONG max_depth = m_max_depth;
		if (depth > max_depth) {
			InterlockedExchange(&m_max_depth, depth);
		}
		// (2) 扩展当前状态，
		auto it = m_open_list.begin();
		if (m_thread_num > 1)		EnumerateOp_Thread(m_cur_state, it);
		else						EnumerateOp(m_cur_state, it);
		// (3) 移除当前状态      
		// 清除父节点
		LOG_DEBUG_(1, L"out => state=%p, ref=%d, parent=%p, parent ref=%d", m_cur_state, m_cur_state->m_ref, m_cur_state->m_parent, (m_cur_state->m_parent ? m_cur_state->m_parent->m_ref : -1));
		m_states.put(m_cur_state);
	}
#endif
	return 0;
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
	fclose(m_log_file);
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
	if (ir != ERR_OK)
	{	// 输出trace
		if (tester->m_cur_state)
		{
			tester->OutputTrace(tester->m_cur_state);
			tester->m_states.put(tester->m_cur_state);
		}
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
	pt_sum.add_child(L"exhaustive_summary", sum);
}

//#define MAX_OPERATION 100

#ifdef THREAD_QUEUE
#define DoFsOperator_ DoFsOperator_Queue
#elif defined THREAD_POOL
#define DoFsOperator_ DoFsOperator_Pool
#elif defined THREAD_MULTI
#define DoFsOperator_ DoFsOperator_Thread
#endif

bool CExTester::EnumerateOp_Thread(CFsState* cur_state, std::list<CFsState*>::iterator& insert)
{
	LOG_STACK_TRACE();
	UINT context_id = 0;
	UINT first_op = m_op_sn;

	CReferenceFs& ref_fs = cur_state->m_ref_fs;
	auto endit = ref_fs.End();
	auto it = ref_fs.Begin();
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

		TRACE_ENTRY op;

		if (isdir)
		{	// 目录
			UINT child_num = file.child_num();
			if (child_num >= m_max_child_num) continue;
			if (file.depth() >= (m_max_dir_depth - 1)) continue;

			if (ref_fs.GetFileNumber() >= MAX_FILE_NUM) continue;

			for (auto op_it = m_dir_op_set.begin(); op_it != m_dir_op_set.end(); ++op_it)
			{
				char fn[3];	//文件名，随机产生2字符
				GenerateFn(fn, 2);
				op.file_path = (path.size() > 1) ? (path + "\\" + fn) : (path + fn);

				switch (*op_it)
				{
				case OP_CODE::OP_FILE_CREATE: {
					op.op_code = OP_FILE_CREATE;
					DoFsOperator_(cur_state, op, m_works + (context_id++));

					break; }

				case OP_CODE::OP_DIR_CREATE:
					op.op_code = OP_DIR_CREATE;
					DoFsOperator_(cur_state, op, m_works + (context_id++));
					break;

				case OP_CODE::OP_MOVE:
					break;
				}
				// 测试后处理
			}
		}
		else
		{	// 文件
			for (auto op_it = m_file_op_set.begin(); op_it != m_file_op_set.end(); ++op_it)
			{
				FSIZE offset = (FSIZE)(file_len * ((float)(rand()) / RAND_MAX));
				FSIZE len = (FSIZE)((m_max_file_size - offset) * ((float)(rand()) / RAND_MAX));
				switch (*op_it)
				{
				case OP_CODE::OP_FILE_WRITE:
					if (file.write_count() < m_max_file_op)
					{
						op.op_code = OP_FILE_WRITE;
						op.file_path = path;
						op.offset = offset;
						op.length = len;
						DoFsOperator_(cur_state, op, m_works + (context_id++));
					}
					break;

				case OP_CODE::OP_FILE_DELETE:
					op.op_code = OP_FILE_DELETE;
					op.file_path = path;
					DoFsOperator_(cur_state, op, m_works + (context_id++));
					break;

				case OP_CODE::OP_MOVE:
					break;
				}
			}
		}
		if (context_id > MAX_WORK_NR) THROW_ERROR(ERR_APP, L"context overflow");
	}
	if (context_id > m_max_work) m_max_work = context_id;

	// 等待线程结束
	if (context_id == 0) return true;



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
			THROW_ERROR(ERR_APP, L"test failed, code=%d", m_works[ii].ir);
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
	return true;
}

bool CExTester::EnumerateOp(CFsState* cur_state, std::list<CFsState*>::iterator& insert)
{
	LOG_STACK_TRACE();
	int context_id = 0;
	UINT first_op = m_op_sn;

	CReferenceFs& ref_fs = cur_state->m_ref_fs;
	auto endit = ref_fs.End();
	auto it = ref_fs.Begin();
	for (; it != endit; it++)
	{
		const CReferenceFs::CRefFile& file = ref_fs.GetFile(it);
		bool isdir = ref_fs.IsDir(file);

		std::string path;
		ref_fs.GetFilePath(file, path);

		DWORD checksum;
		FSIZE file_len;
		ref_fs.GetFileInfo(file, checksum, file_len);

		TRACE_ENTRY op;
		if (isdir)
		{	// 目录
			UINT child_num = file.child_num();
			if (child_num >= m_max_child_num) continue;
			if (file.depth() >= (m_max_dir_depth-1)) continue;

			if (ref_fs.GetFileNumber() >= MAX_FILE_NUM) continue;

			for (auto op_it = m_dir_op_set.begin(); op_it != m_dir_op_set.end(); ++op_it)
			{
				char fn[3];	//文件名，随机产生2字符
				GenerateFn(fn, 2);

				switch (*op_it)
				{
				case OP_CODE::OP_FILE_CREATE: {
					op.op_code = OP_FILE_CREATE;
					op.file_path = (path.size() > 1) ? (path + "\\" + fn) : (path + fn);
					DoFsOperator(cur_state, op, insert);
					context_id++;
					break; }

				case OP_CODE::OP_DIR_CREATE:
					// 深度限制
					op.op_code = OP_DIR_CREATE;
					op.file_path = (path.size() > 1) ? (path + "\\" + fn) : (path + fn);
					DoFsOperator(cur_state, op, insert);
					context_id++;
					break;

				case OP_CODE::OP_MOVE:
					break;
				}
				// 测试后处理
			}
		}
		else
		{	// 文件
			for (auto op_it = m_file_op_set.begin(); op_it != m_file_op_set.end(); ++op_it)
			{
				FSIZE offset = (FSIZE)( file_len * ((float)(rand())/ RAND_MAX));
				FSIZE len = (FSIZE)((m_max_file_size-offset) * ((float)(rand())/ RAND_MAX));
				switch (*op_it)
				{
				case OP_CODE::OP_FILE_WRITE:
					if (file.write_count() < m_max_file_op)
					{
						op.op_code = OP_FILE_WRITE;
						op.file_path = path;
						op.offset = offset;
						op.length = len;
						DoFsOperator(cur_state, op, insert);
						context_id++;
					}
					break;

				case OP_CODE::OP_FILE_DELETE:
					op.op_code = OP_FILE_DELETE;
					op.file_path = path;
					DoFsOperator(cur_state, op, insert);
					context_id++;
					break;

				case OP_CODE::OP_MOVE:
					break;
				}
			}
		}
		for (auto op_it = m_fs_op_set.begin(); op_it != m_fs_op_set.end(); ++op_it)
		{
			switch (*op_it)
			{
			case OP_CODE::OP_DEMOUNT_MOUNT:
				op.op_code = OP_DEMOUNT_MOUNT;
				DoFsOperator(cur_state, op, insert);
				break;
			case OP_CODE::OP_POWER_OFF_RECOVER:
				op.op_code = OP_POWER_OFF_RECOVER;
				DoFsOperator(cur_state, op, insert);
				break;
			}
		}
	}
	UpdateFsParam(cur_state->m_real_fs);

	double runtime = RUNNING_TIME;
	fprintf_s(m_log_performance, "%d,%.1f,%d,%.1f\n", first_op, runtime, context_id, runtime / context_id);

	return true;
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
	LOG_DEBUG(L"submit work: context=%p, op=%d, code=%d, work=%p", context, m_op_sn-1, op.op_code, context->work_item);
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

// for both single thread and multi thread
ERROR_CODE CExTester::FsOperatorCore(CFsState* state, TRACE_ENTRY& op)
{
	ERROR_CODE ir = ERR_OK;
	// (2) 在新的测试状态上执行测试
	std::string &path =  op.file_path;
//	jcvos::Utf8ToUnicode(wpath, op.file_path);
	switch (op.op_code)
	{
	case OP_FILE_CREATE:
		ir = TestCreateFile(state, op.file_path);
		break;
	case OP_DIR_CREATE:
		ir = TestCreateDir(state, op.file_path);
		break;

	case OP_FILE_WRITE:
		ir = TestWriteFile(state, path, op.offset, op.length);
		break;

	case OP_FILE_DELETE:
		ir = TestDeleteFile(state, path);
		break;

		//	case OP_FILE_OVERWRITE:
	case OP_DIR_DELETE:
	case OP_MOVE:
		break;
	case OP_DEMOUNT_MOUNT:
		ir = TestMount(state);
		break;

	case OP_POWER_OFF_RECOVER:
		ir = TestPowerOutage(state);
		break;
	}
	// (3) 检查测试结果
	if (ir != ERR_OK)
	{
		THROW_ERROR(ERR_APP, L"run test error, code=%d op=%d, file=%s, ", ir, op.op_code, op.file_path.c_str());
	}
	//		ERROR_CODE err = ERR_OK;
	ir = Verify(state);
	if (ir != ERR_OK) THROW_ERROR(ERR_APP, L"verify failed, code=%d", ir);

	//	new_state->OutputState(m_log_file);
	return ir;
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
	catch (...)
	{
		ir = ERR_GENERAL;
	}
	context->ir = ir;
	LOG_DEBUG(L"complete work: context=%p, op=%d, code=%d", context, op.op_sn, op.op_code);

//	SetEvent(context->hevent);
}

ERROR_CODE CExTester::DoFsOperator(CFsState* cur_state, TRACE_ENTRY& op, std::list<CFsState*>::iterator& insert)
{
	LOG_STACK_PERFORM(L"fs_operation");
	UINT opsn = m_op_sn;
	LOG_DEBUG(L"start work: op=%d, code=%d", opsn, op.op_code);

	JCASSERT(cur_state);
	// 标准测试流程：
	// (1) 复制测试状态，
	CFsState* new_state = m_states.duplicate(cur_state);
	InterlockedIncrement(&(cur_state->m_ref));

	new_state->m_op = op;
	new_state->m_op.op_sn = m_op_sn++;
	ERROR_CODE ir = ERR_OK;

	try {
		ir = FsOperatorCore(new_state, op);

		//	new_state->OutputState(m_log_file);
		if (m_closed.Check(new_state))
		{
			m_states.put(new_state);
			LOG_DEBUG(L"complete work: op=%d, code=%d", opsn, op.op_code);
			return ERR_OK;
		}
		if (new_state)
		{
			m_open_list.insert(insert, new_state);
			LOG_DEBUG_(1, L"in => state=%p, ref=%d, parent=%p, parent ref=%d", new_state, new_state->m_ref, new_state->m_parent, (new_state->m_parent ? new_state->m_parent->m_ref : -1));
		}
	}
	catch (...)
	{
		m_states.put(new_state);
		LOG_DEBUG_(1, L"test failed, state=%p", new_state);
		ir = ERR_GENERAL;
		throw;
	}
	LOG_DEBUG(L"complete work: op=%d, code=%d", opsn, op.op_code);

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
	UINT total_item = src_info.dir_nr + src_info.file_nr;
	max_update(m_total_item_num, total_item);
	max_update(m_file_num, src_info.file_nr);
	max_update(m_logical_blks, src_info.used_blks);
	m_total_blks = src_info.total_blks;
	UINT ph_blks = src_info.total_blks - src_info.free_blks;
	max_update(m_physical_blks, src_info.physical_blks);
	max_update64(m_host_write, src_info.total_host_write);
	max_update64(m_media_write, src_info.total_media_write);
	min_update(m_free_blks, src_info.free_blks);
}

ERROR_CODE CExTester::Verify(CFsState* cur_state)
{
	LOG_STACK_PERFORM(L"verification");
	ERROR_CODE err = ERR_OK;
	TEST_LOG("[BEGIN VERIFY]\n");

	CReferenceFs& ref = cur_state->m_ref_fs;
	IFsSimulator* fs = cur_state->m_real_fs;

	// 获取文件系统信息：文件、目录总数；文件数量；逻辑饱和度；物理饱和度；空闲块；host write; media write
	FS_INFO fs_info;
	fs->GetFsInfo(fs_info);
	UINT total_item = fs_info.dir_nr + fs_info.file_nr;
	UINT ph_blks = fs_info.total_blks - fs_info.free_blks;

	// TODO: os需要保存file number以供检查
	//UINT dir_nr = ref.m_dir_num, file_nr = ref.m_file_num;
	//if (dir_nr != fs_info.dir_nr) {
	//	THROW_ERROR(ERR_APP, L"directory number does not match, ref:%d, fs:%d", dir_nr, fs_info.dir_nr);
	//}
	//if (file_nr != fs_info.file_nr) {
	//	THROW_ERROR(ERR_APP, L"file number does not match, ref:%d, fs:%d", file_nr, fs_info.file_nr);
	//}
	FSIZE total_file_blks = 0;

	std::string str_encode;
	ref.GetEncodeString(str_encode);

	size_t checked_file = 0, checked_total = 0;
	auto endit = ref.End();
	for (auto it = ref.Begin(); it != endit; ++it)
	{
		const CReferenceFs::CRefFile& ref_file = ref.GetFile(it);
		std::string path;
		ref.GetFilePath(ref_file, path);
		if (path == "\\") continue;	//不对根目录做比较

		checked_total++;
		bool dir = ref.IsDir(ref_file);
		ref_file.GetEncodeString(str_encode);
		DWORD access = 0;
		DWORD flag = 0;
		if (dir) { flag |= FILE_FLAG_BACKUP_SEMANTICS; }
		else { access |= GENERIC_READ; }

		TEST_LOG("  check %s: %s\n", dir ? "DIR " : "FILE", path.c_str());

//		std::wstring wpath;
//		jcvos::Utf8ToUnicode(wpath, path);
		NID fid = fs->FileOpen(path);

		if (fid == INVALID_BLK)
		{
			err = ERR_OPEN_FILE;
			THROW_ERROR(ERR_APP, L"failed on open file=%s", path.c_str());
//			return err;
		}

		if (!dir)
		{
			checked_file++;
			DWORD ref_checksum;
			FSIZE ref_len, file_secs = fs->GetFileSize(fid);
			ref.GetFileInfo(ref_file, ref_checksum, ref_len);
			total_file_blks += ref_len;

//			TEST_LOG(L" ref size=%d, file size=%d, checksum=0x%08X", ref_len, cur_file_size, ref_checksum);
			if (ref_len != file_secs)
			{
				err = ERR_WRONG_FILE_SIZE;
				TEST_ERROR("file length does not match ref=%d, file=%d", ref_len, file_secs);
				THROW_ERROR(ERR_APP, L"verify file failed, length does not match");
//				return err;
			}
			FILE_DATA data[MAX_FILE_BLKS];
			size_t page_nr = fs->FileRead(data, fid, 0, file_secs);
			FSIZE ss = 0;

			int compare =0;
			for (size_t ii =0; ii<page_nr; ++ii, ss++)
			{
				if (data[ii].fid != fid || data[ii].offset != ss) {
//					THROW_ERROR(ERR_APP, L"read file does not match, fid=%d, lblk=%d, page_fid=%d, page_offset=%d",
//						fid, ss, data[ii].fid, data[ii].offset);
					TEST_ERROR("file data mismatch, fid=%d, lblk=%d, page_fid=%d, page_offset=%d", fid, ss, data[ii].fid, data[ii].offset);
					compare++;
				}
			}
			if (compare>0) THROW_ERROR(ERR_USER, L"read file mismatch, %d secs", compare);

			//if (cur_file_size == 0)
			//{	// 有可能是over write的roll back造成的，忽略比较
			//	TEST_LOG(L" ignor comparing");
			//}
			//else
			//{
			//	TEST_LOG(L" compare");
			//	if (ref_len != cur_file_size)
			//	{
			//		err = ERR_WRONG_FILE_SIZE;
			//		TEST_ERROR(L"file length does not match ref=%zd, file=%zd", ref_len, cur_file_size);
			//		m_fs->FileClose(fid);
			//		THROW_ERROR(ERR_USER, L"file length does not match ref=%d, file=%d", ref_len, cur_file_size);
			//		break;
			//	}
			//	DWORD checksum = CalFileChecksum(fid);
			//	if (checksum != ref_checksum)
			//	{
			//		err = ERR_WRONG_FILE_DATA;
			//		TEST_ERROR(L"checksum does not match, ref=0x%08X, file=0x%08X", ref_checksum, checksum);
			//		m_fs->FileClose(fid);
			//		THROW_ERROR(ERR_USER, L"checksum does not match, ref=0x%08X, file=0x%08X", ref_checksum, checksum);
			//		break;
			//	}
			//}
		}
		fs->FileClose(fid);

	}
	TEST_LOG("[END VERIFY], total checked=%zd, file checked= %zd\n", checked_total, checked_file);
	return err;
}

ERROR_CODE CExTester::TestCreateFile(CFsState* cur_state, const std::string& path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;
	// 检查自己点数量是否超标
	// create full path name
	bool create_result = false;

	NID fid = real_fs->FileCreate(path);
	if (fid != INVALID_BLK)
	{
		create_result = true;
		real_fs->FileClose(fid);
	}

	// check if doubled name, update ref fs
	if (ref.IsExist(path))
	{	// 文件已经存在，要求返回false
		TEST_LOG("[OPERATE ](%d) CREATE FILE, path=%s, existing file\n", cur_state->m_op.op_sn, path.c_str());
		if (create_result)
		{
			err = ERR_CREATE_EXIST;
			TEST_ERROR("create a file which is existed path=%s.", path.c_str());
		}
	}
	else
	{	// create file in fs
		TEST_LOG("[OPERATE ](%d) CREATE FILE, path=%s, new file\n", cur_state->m_op.op_sn, path.c_str());
		ref.AddPath(path, false, fid);
		if (!create_result)
		{
			err = ERR_CREATE;
			TEST_ERROR("failed on creating file fn=%s", path.c_str());
		}
	}
//	TEST_CLOSE_LOG;

	return err;
}

ERROR_CODE CExTester::TestDeleteFile(CFsState* cur_state, const std::string& path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;
	TEST_LOG("[OPERATE ](%d) DELETE FILE, path=%s,", cur_state->m_op.op_sn, path.c_str());

	real_fs->FileDelete(path);
	ref.RemoveFile(path);

	TEST_CLOSE_LOG;
	return err;
}

ERROR_CODE CExTester::TestMount(CFsState* cur_state)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);

	real_fs->Unmount();
	real_fs->Reset();
	real_fs->Mount();
	TEST_LOG("[OPERATE ](%d) DEMOUNT MOUNT\n", cur_state->m_op.op_sn);
//	TEST_CLOSE_LOG;
	return ERR_OK;
}

ERROR_CODE CExTester::TestPowerOutage(CFsState* cur_state)
{
	return ERR_OK;
}

ERROR_CODE CExTester::TestCreateDir(CFsState* cur_state, const std::string& path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;
	// 检查自己点数量是否超标
	// create full path name
	TEST_LOG("[OPERATE ](%d) CREATE DIR_, path=%s,", cur_state->m_op.op_sn, path.c_str());

//	std::wstring file_path = path;
	//std::wstring wpath;
	//jcvos::Utf8ToUnicode(wpath, path);
	bool create_result = false;
	NID fid = real_fs->DirCreate(path);
	if (fid != INVALID_BLK)
	{
		create_result = true;
//		real_fs->FileClose(fid);
	}

	// check if doubled name, update ref fs
	if (ref.IsExist(path))
	{	// 文件已经存在，要求返回false
		TEST_LOG(" existing dir");
		//err = OK_ALREADY_EXIST;
		if (create_result)
		{
			err = ERR_CREATE_EXIST;
			TEST_ERROR("create a file which is existed path=%s.", path.c_str());
		}
	}
	else
	{	// create file in fs
		TEST_LOG(" new dir");
		ref.AddPath(path, true, fid);
		if (!create_result)
		{
			err = ERR_CREATE;
			TEST_ERROR("failed on creating file fn=%s", path.c_str());
		}
	}
	TEST_CLOSE_LOG;
	return err;
}

ERROR_CODE CExTester::TestWriteFile(CFsState* cur_state, const std::string& path, FSIZE offset, FSIZE len)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;
	//	len &= ~3;		// DWORD对齐
	TEST_LOG("[OPERATE ](%d) WriteFile, path=%s, offset=%d, size=%d\n",cur_state->m_op.op_sn, path.c_str(), offset, len);

	//	HANDLE file = CreateFile(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0);
	NID fid = real_fs->FileOpen(path);
	if (fid == INVALID_BLK)
	{
		err = ERR_OPEN_FILE;
		THROW_WIN32_ERROR(L"failed on opening file, fn=%s", path.c_str());
	}

	//	size_t total, used, free, max_files, file_num;
	FS_INFO space_info;
	real_fs->GetFsInfo(space_info);

	FSIZE cur_ref_size;
	DWORD cur_checksum;
	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (!ref_file) THROW_ERROR(ERR_USER, L"cannof find ref file: %s", path.c_str());
	ref.GetFileInfo(*ref_file, cur_checksum, cur_ref_size);

	// get current file length
	FSIZE cur_file_size = real_fs->GetFileSize(fid);
	//	if (cur_len != info.nFileSizeLow && info.nFileSizeLow != 0)
	if (cur_ref_size != cur_file_size)	{
		THROW_ERROR(ERR_USER, L"file length does not match ref=%d, file=%d", cur_ref_size, cur_file_size);
	}

	// 
	FSIZE new_file_size = offset + len;
	if (new_file_size > cur_file_size)
	{	// 当要写入的文件大小超过空余容量时，缩小写入量。
		FSIZE incre_blks = ROUND_UP_DIV(new_file_size - cur_file_size, BLOCK_SIZE);
		if (incre_blks > space_info.free_blks)
		{
			incre_blks = space_info.free_blks;
			new_file_size = cur_file_size + incre_blks * BLOCK_SIZE;
			if (new_file_size < offset)
			{
				len = 0; new_file_size = offset;
			}
			else len = new_file_size - offset;
		}
	}
	real_fs->FileWrite(fid, offset, len);
	if (cur_file_size > new_file_size) new_file_size = cur_file_size;

	//DWORD checksum = CalFileChecksum(fid);
	DWORD checksum = fid;
//	ref_file->m_pre_size = (int)cur_ref_size;
	ref.UpdateFile(path, checksum, new_file_size);
//	TEST_LOG("\t current size=%d, new size=%d", cur_file_size, new_file_size);
	//CloseHandle(file);
	real_fs->FileClose(fid);
//	TEST_LOG(", closed");

//	TEST_CLOSE_LOG;
	return err;
}
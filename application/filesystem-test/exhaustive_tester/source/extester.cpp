///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"
#include <Psapi.h>
#include <boost/property_tree/json_parser.hpp>

LOCAL_LOGGER_ENABLE(L"extester", LOGGER_LEVEL_DEBUGINFO);


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Help functions ==
#define FILENAME_NR (64)
static const char* FILENAME_LIST[] = {
	"A",		"B",		"C",		"D",		"E",		"F",		"G",		"H",
	"KA",		"SA",		"TA",		"HA",		"KI",		"SI",		"TI",		"HI",
	"CAT",		"DOG",		"EAT",		"PEN",		"BED",		"SKY",		"CUP",		"HAT",
	"BOOK",		"FISH",		"PLAY",		"TREE",		"MOON",		"LOVE",		"TIME",		"WINE",
	"APPLE",	"HAPPY",	"WATER",	"LIGHT",	"BEGIN",	"MUSIC",	"GREEN",	"WORLD",
	"FREIND",	"SUMMER",	"GARDEN",	"BRIGHT",	"TRAVEL",	"YELLOW",	"FAMILY",	"OFFICE",
	"WEATHER",	"MORNING",	"SCIENCE",	"HISTORY",	"PERFECT",	"COUNTRY",	"FREEDOM",	"CAPITAL",
	"BIRTHDAY",	"ELEPHANT",	"HOSPITAL",	"MOUNTAIN", "POWERFUL",	"COMPLETE",	"MIDNIGHT",	"LANGUAGE"
};

int GenerateFn(char* fn, int len)
{
	int index = rand() % FILENAME_NR;
	int src_len = (index / 8) + 1;
	int copy_len = min(src_len, len);
//	if (src_len >)
//	strcpy_s(fn, len, FILENAME_LIST[index]);
	memcpy_s(fn, len, FILENAME_LIST[index], copy_len);
	fn[copy_len] = 0;
//	return (int)strlen(fn);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Exahustive Tester ==
CExTester::CExTester(void)
{
	m_states.Initialize(4096);

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
	m_stop_on_error = config.get<bool>(L"stop_on_error", false);
	m_skip_check_file_data = config.get<bool>(L"skip_check_file_data", false);

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
	LOG_DEBUG(L"[rollback], initial io nr=%d", m_fs_factory->GetCacheNum());
	m_fs_factory->Clone(fs);
	init_state->Initialize(fs);
	//init_state->m_io_nr = m_fs_factory->GetCacheNum();

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

TRACE_ENTRY* op_index(std::vector<TRACE_ENTRY>& ops, size_t& index)
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

void AddOp(TRACE_ENTRY* ops, size_t op_size, size_t& index, OP_CODE op_code, const char* file_path)
{
	//	TRACE_ENTRY* op = op_index(ops, index);
	if (index >= op_size)	THROW_ERROR(ERR_APP, L"operation list overflow, size=%d", op_size);
	TRACE_ENTRY* op = ops + index;
	index++;

	op->op_code = op_code;
	strcpy_s(op->file_path, file_path);
	op->dst[0] = 0;
	op->offset = 0;
	op->length = 0;
	op->fid = 0;
}

void AddOp(TRACE_ENTRY* ops, size_t op_size, size_t& index, OP_CODE op_code, const char* file_path,
	UINT fid, FSIZE offset, FSIZE length)
{
	if (index >= op_size)	THROW_ERROR(ERR_APP, L"operation list overflow, size=%d", op_size);
	TRACE_ENTRY* op = ops + index;
	index++;

	op->op_code = op_code;
	strcpy_s(op->file_path, file_path);
	op->dst[0] = 0;
	op->offset = offset;
	op->length = length;
	op->fid = fid;
}

void AddOp(TRACE_ENTRY* ops, size_t op_size, size_t& index, OP_CODE op_code)
{
	if (index >= op_size)	THROW_ERROR(ERR_APP, L"operation list overflow, size=%d", op_size);
	TRACE_ENTRY* op = ops + index;
	index++;

	op->file_path[0] = 0;
	op->dst[0] = 0;
	op->op_code = op_code;
	op->offset = 0;
	op->length = 0;
	op->fid = 0;
}

void AddOp(TRACE_ENTRY* ops, size_t op_size, size_t& index, OP_CODE op_code, UINT rollback)
{
	if (index >= op_size)	THROW_ERROR(ERR_APP, L"operation list overflow, size=%d", op_size);
	TRACE_ENTRY* op = ops + index;
	index++;

	op->file_path[0] = 0;
	op->dst[0] = 0;
	op->op_code = op_code;
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
	for (; it != endit; ++it)
	{
		const CReferenceFs::CRefFile& file = ref_fs.GetFile(it);
		bool isdir = ref_fs.IsDir(file);

		const char* path = file.get_path();

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
			if (path[1] != 0 || path[0] != '\\')
			{
				if (child_num == 0 ) {	AddOp(ops, op_size, index, OP_DIR_DELETE, path); }
				// rename
				AddOp(ops, op_size, index, OP_MOVE, path);
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
				// rename
				AddOp(ops, op_size, index, OP_MOVE, path);

			}
		}
	}

	AddOp(ops, op_size, index, OP_DEMOUNT_MOUNT);
	// 生成对power off测试的op
	if (m_check_power_loss)
	{
		if (cur_state->m_op.op_code == OP_FILE_WRITE) {
			AddOp(ops, op_size, index, OP_POWER_OFF_RECOVER, (UINT)0);
		}
		else {
			IFsSimulator* real_fs = cur_state->m_real_fs;
			UINT io_nr = real_fs->GetCacheNum();
			for (UINT ii = 0; ii < io_nr && index < op_size; ++ii)
			{
				AddOp(ops, op_size, index, OP_POWER_OFF_RECOVER, ii);
			}
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
			// 当达到最大深度时，所以此检查
			ir = Verify(cur_state);
			if (ir != ERR_OK)
			{
				char dd[32];
				sprintf_s(dd, "[depth=%d], ", cur_state->m_depth);
				std::string mm = dd;
//				std::string* err_msg = cur_state->m_err_msg;
				if (cur_state->m_err_msg) mm += *cur_state->m_err_msg;

//				sprintf_s(str_msg, "[depth=%d], %S", cur_state->m_depth, err_msg?err_msg->c_str():L"");
				OutputTrace_Thread(cur_state, ir, mm, (DWORD)(ir), TRACE_REF_FS | TRACE_REAL_FS | TRACE_FILES | TRACE_JSON);
			}

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
		if (m_stop_on_error && (ir != ERR_OK))
		{
			break;
		}
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
		LOG_CRITICAL(L"test completed with code=%d", ir);
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

#ifdef USE_HEAP
	size_t closed = m_closed.size();
#else
	size_t closed = m_closed_size;
#endif

	// 输出到concole
	wprintf_s(L"ts=%llds, op=%d, depth=%d, closed=%zd, open=%zd, mem=%.1fMB, max_mem=%.1fMB\n"
		L"files=%d, logic=%d, phisic=%d, total=%d, free=%d, host write=%lld, media write= %lld\n",
		ts, m_op_sn, m_max_depth, closed, m_open_list.size(), mem_cost, m_max_memory_cost,
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
#ifdef USE_HEAP
	size_t state = m_closed.size();
#else
	size_t state = m_closed_size;
#endif
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
	UpdateFsParam(cur_state->m_real_fs);
	return ERR_OK;
}

void AppendRandomPath(char* file_path)
{
	//		std::string path = (op.file_path.size() > 1) ? (op.file_path + "\\" + fn) : (op.file_path + fn);
	size_t path_len = strlen(file_path);
	char* ptr = nullptr;
	if (path_len > 1)
	{
		ptr = file_path + path_len;
		*ptr = '\\';
		ptr++;
		path_len++;
	}
	else {	// 根目录
		ptr = file_path + 1;
	}
	GenerateFn(ptr, (int)(MAX_PATH_SIZE - path_len));
}

// for both single thread and multi thread
ERROR_CODE CExTester::FsOperatorCore(CFsState* state, TRACE_ENTRY& op)
{
	ERROR_CODE ir = ERR_OK;
	// (2) 在新的测试状态上执行测试
	bool is_power_test = false;
	switch (op.op_code)
	{
	case OP_FILE_CREATE: {
		AppendRandomPath(op.file_path);
		op.offset = 0;
		op.length = 0;
		ir = TestCreateFile(state, op.file_path);
		break; }
	case OP_DIR_CREATE: {
		AppendRandomPath(op.file_path);
		op.offset = 0;
		op.length = 0;
		ir = TestCreateDir(state, op.file_path);
		break; }

	case OP_MOVE: {
		strcpy_s(op.dst, op.file_path);
		char * ptr = strrchr(op.dst, '\\');
		if ( ptr == nullptr || *ptr == 0) {
			THROW_ERROR(ERR_APP, L"[err] wrong source file name for move: %S", op.file_path);
		}
		ptr++;
		GenerateFn(ptr, (int)(MAX_PATH_SIZE - (ptr - op.dst)) );
		ir = TestMoveFile(state, op.file_path, op.dst);
		break;
	}

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

	case OP_DEMOUNT_MOUNT:
		op.offset = 0;
		op.length = 0;
		op.file_path[0] = 0;
		ir = TestMount(state);
		break;

	case OP_POWER_OFF_RECOVER:
		// power outage的测试，导致io逆流，重新计算起点io
//		op.file_path = "";
		op.file_path[0] = 0;
		ir = TestPowerOutage(state, op.rollback);
		state->m_unsafe_mount_cnt++;
		is_power_test = true;
		break;
	}
	if (state->m_result==ERR_OK) state->m_result = ir;
	// (3) 检查测试结果
	if (ir != ERR_OK && ir != ERR_NO_OPERATION)
	{
		//THROW_FS_ERROR(ir, L"error op=%d, msg=%S", op.op_code, state->m_err_msg ? state->m_err_msg->c_str() : "");
		FS_BREAK;
		size_t buf_size = 32;
		if (state->m_err_msg) buf_size += state->m_err_msg->size()*2;
		CFsException err(ir, __STR2WSTR__(__FUNCTION__), __LINE__, buf_size, L"error op=%d, msg=%S", op.op_code, state->m_err_msg ? state->m_err_msg->c_str() : "");
		throw err;
	}
	return ir;
}

ERROR_CODE CExTester::DoFsOperator(CFsState* cur_state, TRACE_ENTRY& op, std::list<CFsState*>::iterator& insert)
{
//	LOG_STACK_PERFORM(L"fs_operation");
	UINT opsn = m_op_sn;
	JCASSERT(cur_state);
	// 标准测试流程：
	// (1) 复制测试状态，复制的状态，要么放入open list，在FinishTest中回收，要么在返回前回收。
	CFsState* new_state = m_states.duplicate(cur_state);
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

	max_update(m_logical_blks, src_info.used_blk);
	m_total_blks = src_info.main_blk_nr;
	UINT ph_blks = src_info.used_seg*BLOCK_PER_SEG;
	max_update(m_physical_blks, src_info.used_seg*BLOCK_PER_SEG);
	max_update64(m_host_write, src_info.host_write);
	max_update64(m_media_write, src_info.media_write);
	min_update(m_free_blks, src_info.free_blk);
}

ERROR_CODE CExTester::Verify(CFsState* state, bool debug)
{
	char msg[256];
	IFsSimulator* real_fs = state->m_real_fs;
	CReferenceFs& ref_fs = state->m_ref_fs;
	return VerifyState(ref_fs, real_fs, msg, debug);
}

ERROR_CODE CExTester::VerifyForPower(CFsState* cur_state, bool debug)
{
	CFsState* state = cur_state;
	IFsSimulator* real_fs = state->m_real_fs;
	ERROR_CODE err = ERR_OK;
	UINT cur_depth = cur_state->m_depth;
	std::string err_msg;
	while (state != nullptr)
	{
		CReferenceFs& ref_fs = state->m_ref_fs;
		LOG_DEBUG(L"Verify state: depth=%d", state->m_depth);
		char msg[256];
		err = VerifyState(ref_fs, real_fs, msg, debug);
		if ((err == ERR_OK) || (err == ERR_NO_OPERATION))
		{
			LOG_DEBUG(L"Verify passed, depth=%d", state->m_depth);
			if (state != cur_state)
			{	// 用正确的状态覆盖原来状态
				cur_state->m_ref_fs.CopyFrom(state->m_ref_fs);
			}
			break;
		}
		err_msg += msg;
		// 对于power off测试，回溯到前一个稳定状态。
//		if ( (state->m_stable == true) /*&& (cur_depth - state->m_depth > 1)*/) break;
		state = state->m_parent;
		LOG_DEBUG(L"Verify failed, rollback state.");
	}
	// 关闭所有文件，
	cur_state->m_ref_fs.Demount();
	cur_state->SetErrorMessage(err_msg);
	return err;
}

ERROR_CODE CExTester::VerifyState(CReferenceFs & ref_fs, IFsSimulator * real_fs, char * err_msg, bool debug)
{
	ERROR_CODE ir = ERR_OK;
	size_t checked_file = 0, checked_total = 0;

	// 获取文件系统信息：文件、目录总数；文件数量；逻辑饱和度；物理饱和度；空闲块；host write; media write
	try {
		LOG_DEBUG(L"\tCheck file system");
		FS_INFO fs_info;
		real_fs->GetFsInfo(fs_info);
		//UINT ph_blks = fs_info.total_blks - fs_info.free_blks;

		UINT ref_dir_nr = ref_fs.m_dir_num, ref_file_nr = ref_fs.m_file_num;
		UINT real_file_nr = 0, real_dir_nr = 0;
		real_fs->GetFileDirNum(0, real_file_nr, real_dir_nr);		// TODO: os需要保存file number以供检查

		if (real_dir_nr != ref_dir_nr) {
			LOG_ERROR(L"[err] [code=%d] directory number does not match, ref:%d, fs:%d", ERR_WRONG_FILE_NUM, ref_dir_nr, real_dir_nr);
			sprintf_s(err_msg, 256, "[verify error] directory number does not match, ref:%d, fs:%d\n", ref_dir_nr, real_dir_nr);
			return ERR_WRONG_FILE_NUM;

		}
		if (real_file_nr != ref_file_nr) {
			LOG_ERROR(L"[err] [code=%d] file number does not match, ref:%d, fs:%d", ERR_WRONG_FILE_NUM, ref_file_nr, real_file_nr);
			sprintf_s(err_msg, 256, "[verify error] file number does not match, ref:%d, fs:%d\n", ref_file_nr, real_file_nr);
			return ERR_WRONG_FILE_NUM;
		}
		FSIZE total_file_blks = 0;

		char str_encode[MAX_ENCODE_SIZE];
		ref_fs.Encode(str_encode, MAX_ENCODE_SIZE);

		auto endit = ref_fs.End();
		for (auto it = ref_fs.Begin(); it != endit; ++it)
		{
			const CReferenceFs::CRefFile& ref_file = ref_fs.GetFile(it);
			const char* path = ref_file.get_path();
			if (path[0] == '\\' && path[1] == 0) continue;

			checked_total++;
			bool dir = ref_fs.IsDir(ref_file);
			ref_fs.Encode(str_encode, MAX_ENCODE_SIZE);
			DWORD access = 0;
			DWORD flag = 0;
			if (dir) { flag |= FILE_FLAG_BACKUP_SEMANTICS; }
			else { access |= GENERIC_READ; }

			LOG_DEBUG(L"\tCheck %s: %S", dir ? L"DIR " : L"FILE", path);
			_NID fid = INVALID_BLK;
			ir = real_fs->FileOpen(fid, path);
			if (ir == ERR_MAX_OPEN_FILE)
			{
				ir = ERR_OK;
				continue;		// 打开文件已经超过上限
			}

			if (ir != ERR_OK || is_invalid(fid) )
			{
				if (ir == ERR_OK) { ir = ERR_OPEN_FILE; }
				LOG_ERROR(L"[err] [code=%d], failed on open file=%S", ir, path);
				sprintf_s(err_msg, 256, "[verify error] failed on open file=%s, code=%d\n", path, ir);
				return ir;
			}
			if (debug)
			{
				char _dump_flag[12];
				sprintf_s(_dump_flag, "file:%d", fid);
				real_fs->DumpLog(stderr, _dump_flag);
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
				if (is_valid(fid) ) real_fs->FileClose(fid);
				LOG_ERROR(L"[err] [code=%d] file length does not match ref=%d, file=%d", ERR_VERIFY_FILE, ref_len, file_secs);
				sprintf_s(err_msg, 256, "[verify error] file len mismatch, file=%s ref=%d, file=%d\n", path, ref_len, file_secs);
				return ERR_VERIFY_FILE;
			}
			if (!m_skip_check_file_data)
			{
				FILE_DATA data[MAX_FILE_BLKS];
				size_t page_nr = real_fs->FileRead(data, fid, 0, file_secs);
				FSIZE ss = 0;

				int compare = 0;
				for (size_t ii = 0; ii < page_nr; ++ii, ss++)
				{
					if (data[ii].fid != fid || data[ii].offset != ss) {
						LOG_ERROR(L"file data mismatch, fid=%d, lblk=%d, page_fid=%d, page_offset=%d", fid, ss, data[ii].fid, data[ii].offset);
						compare++;
					}
				}
				if (compare > 0) {
					if (is_valid(fid)) real_fs->FileClose(fid);
					LOG_ERROR(L"[err] [code=%d] read file mismatch, %d secs", ERR_VERIFY_FILE, compare);
					sprintf_s(err_msg, 256, "[verify error] compare error, file=%s secs=%d\n", path, compare);
					return ERR_VERIFY_FILE;
				}
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

ERROR_CODE CExTester::TestCreateFile(CFsState* cur_state, const char* path)
{
//	if (path.size() >= MAX_PATH_SIZE) return ERR_NO_OPERATION;

	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE ir = ERR_OK;

	// 检查自己点数量是否超标
	_NID fid = INVALID_BLK;
	try {
		ir = real_fs->FileCreate(fid, path);
	}
	catch (jcvos::CJCException& err)	{
		CFsException* _err = dynamic_cast<CFsException*>(&err);
		if (_err) ir = _err->get_error_code();
		else ir = ERR_UNKNOWN;
	}
	if (ir != ERR_OK) {
		cur_state->m_result = ir;
		LOG_ERROR(L"[ir] failed on creating file %S, err(%d):%s", path, ir, CFsException::ErrCodeToString(ir));
	}

	if (ir == ERR_MAX_OPEN_FILE || ir == ERR_NO_SPACE )
	{	// 达到文件系统极限
		return ERR_NO_OPERATION;
	}

	// check if doubled name, update ref fs
	if (ref.IsExist(path))
	{	// 文件已经存在，要求返回false
		TEST_LOG("[OPERATE ](%d) CREATE FILE, path=%s, existing file\n", cur_state->m_op.op_sn, path);
		if (is_valid(fid) ) { ir = ERR_CREATE_EXIST; }		// 不应该创建成功
		else { ir = ERR_NO_OPERATION; }						// 创建失败。测试成功，返回no operation.
	}
	else
	{	// create file in fs
		TEST_LOG("[OPERATE ](%d) CREATE FILE, path=%s, new file\n", cur_state->m_op.op_sn, path);
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

ERROR_CODE CExTester::TestDeleteFile(CFsState* cur_state, const char* path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE ir = ERR_OK;
	TEST_LOG("[OPERATE ](%d) DELETE FILE, path=%s,", cur_state->m_op.op_sn, path);

	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (ref_file == nullptr) THROW_ERROR(ERR_APP, L"file %S not in ref fs", path);
	try {
		real_fs->FileDelete(path);
	}
	catch (jcvos::CJCException& err) {
		CFsException* _err = dynamic_cast<CFsException*>(&err);
		if (_err) {
			ir = _err->get_error_code();
			cur_state->m_result = ir;
			cur_state->SetErrorMessage(_err->what());
		}
		else ir = ERR_UNKNOWN;
	}
	if (ir==ERR_OK) ref.RemoveFile(path);
	TEST_CLOSE_LOG;
	return ir;
}

ERROR_CODE CExTester::TestDeleteDir(CFsState* cur_state, const char* path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE ir = ERR_OK;
	TEST_LOG("[OPERATE ](%d) DELETE DIR , path=%s,", cur_state->m_op.op_sn, path);

	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (ref_file == nullptr) THROW_ERROR(ERR_APP, L"file %S not in ref fs", path);

	try {
		ir = real_fs->DirDelete(path);
	}
	catch (jcvos::CJCException& err) {
		CFsException* _err = dynamic_cast<CFsException*>(&err);
		if (_err) {
			ir = _err->get_error_code();
			cur_state->m_result = ir;
			cur_state->SetErrorMessage(_err->what());
		}
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

ERROR_CODE CExTester::TestMount(CFsState* cur_state, bool debug)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs & ref = cur_state->m_ref_fs;
	ref.Demount();

	ERROR_CODE ir = ERR_OK;
	try {
		if (debug) {
			printf_s("Dump before Unmount:\n");
			real_fs->DumpLog(stdout, "");
		}
		real_fs->Unmount();
		real_fs->Reset(0);
		real_fs->Mount();
		if (debug) {
			printf_s("Dump after Mount:\n");
			real_fs->DumpLog(stdout, "");
		}
		TEST_LOG("[OPERATE ](%d) DEMOUNT MOUNT\n", cur_state->m_op.op_sn);
		ir = Verify(cur_state);
	}
	catch (jcvos::CJCException & err)
	{
		CFsException* e = dynamic_cast<CFsException*>(&err);
		if (e) {
			ir = e->get_error_code();
			cur_state->m_result = ir;
			cur_state->SetErrorMessage(e->what());
		}
		else ir = ERR_UNKNOWN;
		LOG_ERROR(L"[err] test fail during mouting, code=%d, msg=%s", ir, err.WhatT());
	}

	cur_state->m_stable = true;
	return ir;
}

ERROR_CODE CExTester::TestPowerOutage(CFsState* cur_state, UINT rollback, bool debug)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;
	ref.Demount();
	// 获取fs的io list
	// 对每个尝试每个io, 并且检查完整性
	// 选择一个io, mount，做下一步测试
	cur_state->m_stable = false;
	if (debug) {
		printf_s("Dump before power outage");
		real_fs->DumpLog(stdout, "storage");
		LOG_DEBUG(L"Rollback: %d", rollback);
	}
	real_fs->Reset(rollback);
	real_fs->fsck(true);
	real_fs->Mount();
	if (debug) {
		printf_s("Dump after power outage:\n");
		real_fs->DumpLog(stdout, "");
	}
	TEST_LOG("[OPERATE ](%d) POWER_OUTAGE\n", cur_state->m_op.op_sn);

	ERROR_CODE ir = VerifyForPower(cur_state, debug);
	cur_state->m_stable = true;
	return ir;
	//if (ir != ERR_OK) return ir;
	//return ERR_OK;
}

ERROR_CODE CExTester::TestMoveFile(CFsState* cur_state, const char* src, const char* dst)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	if (src == dst) return ERR_NO_OPERATION;

	ERROR_CODE ir = ERR_OK;
	try {
		ir = real_fs->FileMove(src, dst);
	}
	catch (jcvos::CJCException & err) {
		CFsException* _err = dynamic_cast<CFsException*>(&err);
		if (_err) {
			ir = _err->get_error_code();
			cur_state->m_result = ir;
			cur_state->SetErrorMessage(_err->what());
		}
		else ir = ERR_UNKNOWN;
	}
	if (ir == ERR_NO_SPACE) {
		LOG_ERROR(L"[err] no space to move file from %S to %S", src, dst);
		return ERR_NO_OPERATION;
	}
	if (ir == ERR_CREATE_EXIST ) {
		auto* file = ref.FindFile(dst);
		if (file == nullptr) {
			LOG_ERROR(L"[err] dst file %S is not exist but report error", dst);
			return ir;
		}
		else {
			return ERR_NO_OPERATION;
		}
	}
	if (ir != ERR_OK) {
		LOG_ERROR(L"[err] move file reported err, code=%d", ir);
		return ir;
	}
	ref.MoveFile(src, dst);
	return ir;
}

ERROR_CODE CExTester::TestCreateDir(CFsState* cur_state, const char* path)
{
//	if (path.size() >= MAX_PATH_SIZE) return ERR_NO_OPERATION;

	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE ir = ERR_OK;
	TEST_LOG("[OPERATE ](%d) CREATE DIR_, path=%s,", cur_state->m_op.op_sn, path);

	_NID fid = INVALID_BLK;
	try {
		ir = real_fs->DirCreate(fid, path);
	}
	catch (jcvos::CJCException& err) {
		CFsException* _err = dynamic_cast<CFsException*>(&err);
		if (_err) {
			ir = _err->get_error_code();
			cur_state->m_result = ir;
			cur_state->SetErrorMessage(_err->what());
		}
		else ir = ERR_UNKNOWN;
	}
	if (ir == ERR_MAX_OPEN_FILE || ir == ERR_NO_SPACE) {
		cur_state->m_result = ir;
		return ERR_NO_OPERATION;
	}

	// check if doubled name, update ref fs
	if (ref.IsExist(path))
	{	// 文件已经存在，要求返回false
		if (is_valid(fid))
		{
			ir = ERR_CREATE_EXIST;
			TEST_ERROR("create a file which is existed path=%s.", path);
		}
		else ir = ERR_OK;
	}
	else
	{	// create file in fs
		if (is_valid(fid))
		{
			ref.AddPath(path, true, fid);
			ir = ERR_OK;
		}
	}
	return ir;
}


ERROR_CODE CExTester::TestWriteFileV2(CFsState* cur_state, _NID fid, FSIZE offset, FSIZE len, const char* path)
{
	LOG_STACK_TRACE_EX(L"state=%p", cur_state);
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;

	//随机生成读写长度和偏移
	//	len &= ~3;		// DWORD对齐
	TEST_LOG("[OPERATE ](%d) WriteFile, path=%s, offset=%d, size=%d\n", cur_state->m_op.op_sn, path, offset, len);

	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (!ref_file) {
		cur_state->SetErrorMessage("cannot find ref file");
		THROW_ERROR(ERR_USER, L"cannof find ref file: %s", path);
	}
	if (ref_file->get_fid() != fid) {
		cur_state->SetErrorMessage("file id does not match");
		THROW_ERROR(ERR_APP, L"file id does not match, file=%S, real fid=%d, ref fid=%d", path, fid, ref_file->get_fid());
	}

	if (!ref_file->is_open() ) {
		cur_state->SetErrorMessage("file is not opened");
		THROW_ERROR(ERR_APP, L"file is not opened in ref, file=%S, fid=%d", path, fid);
	}

	FSIZE cur_ref_size;
	DWORD cur_checksum;
	ref.GetFileInfo(*ref_file, cur_checksum, cur_ref_size);
	//FS_INFO space_info;
	//real_fs->GetFsInfo(space_info);

	// get current file length
	FSIZE cur_file_size = real_fs->GetFileSize(fid);
	if (cur_ref_size != cur_file_size) {
		cur_state->SetErrorMessage("file length does not match");
		THROW_ERROR(ERR_USER, L"file length does not match ref=%d, file=%d", cur_ref_size, cur_file_size);
	}

	FSIZE written = real_fs->FileWrite(fid, offset, len);
	FSIZE end_pos = offset + written;

	if (end_pos > cur_file_size) cur_file_size = end_pos;

	DWORD checksum = fid;
	ref.UpdateFile(path, checksum, cur_file_size);
	return err;
}

ERROR_CODE CExTester::TestOpenFile(CFsState* cur_state, const char* path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;

	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (!ref_file) THROW_ERROR(ERR_APP, L"cannof find ref file: %S", path);

	_NID fid = INVALID_BLK;
	err = real_fs->FileOpen(fid, path);
	if (err == ERR_MAX_OPEN_FILE)
	{
		cur_state->m_result = err;
		return ERR_OK;
	}

	ref.OpenFile(*ref_file);
	if (err != ERR_OK || is_invalid(fid))
	{
		THROW_FS_ERROR(err == ERR_OK ? ERR_OPEN_FILE : err, L"failed on opening file, fn=%S", path);
	}
	if (fid != ref_file->get_fid()) {
		cur_state->SetErrorMessage("file id does not match");
		THROW_ERROR(ERR_USER, L"file id does not match, file=%S, real fid=%d, ref fid=%d", path, fid, ref_file->get_fid());
	}
	//ref_file->m_is_open = true;

	TEST_LOG("[OPERATE ](%d) OPEN FILE, path=%s, fid=%d\n", cur_state->m_op.op_sn, path, fid);
	return err;
}

ERROR_CODE CExTester::TestCloseFile(CFsState* cur_state, _NID fid, const char* path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;
	ERROR_CODE ir = ERR_OK;

	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (!ref_file) {
		cur_state->SetErrorMessage("cannot find file in ref");
		THROW_ERROR(ERR_APP, L"cannof find ref file: %S", path);
	}
	if (ref_file->get_fid() != fid) {
		cur_state->SetErrorMessage("file id does not match");
		THROW_ERROR(ERR_APP, L"file id does not match, file=%S, reaf fid=%d, ref fid=%d", path, fid, ref_file->get_fid());
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

	TEST_LOG("[OPERATE ](%d) CLOSE FILE, path=%s, fid=%d\n", cur_state->m_op.op_sn, path, fid)
	return ir;
}
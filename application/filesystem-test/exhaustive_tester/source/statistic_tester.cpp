///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"
#include <Psapi.h>


// 需要输出的参数：
//		step, Dir Nr, File Nr, Safe Mount Count, Unsafe Mount Count, Logical Saturation, Physical Saturation, Free Blocks, Free Segments, Host write, Media write

LOCAL_LOGGER_ENABLE(L"extester.statistic", LOGGER_LEVEL_DEBUGINFO);


CExStatisticTester::CExStatisticTester(void)
{
}

CExStatisticTester::~CExStatisticTester(void)
{
}

int CExStatisticTester::PrepareTest(const boost::property_tree::wptree& config, IFsSimulator* fs, const std::wstring& log_path)
{
	int ir = CExTester::PrepareTest(config, fs, log_path);
	if (ir != 0) return ir;
	m_test_times = config.get<int>(L"test_cycle",10);
	return 0;
}

int CExStatisticTester::PreTest(void)
{
	InitializeCriticalSection(&m_trace_crit);
	m_thread_list = new HANDLE[m_thread_num];

	// 每个work对应一个thread，初始化work
	if (m_thread_num > 1) {
		for (UINT ii = 0; ii < m_thread_num; ++ii)
		{
			m_works[ii].tester = this;
			m_works[ii].state = nullptr;
			m_works[ii].src_state = nullptr;
			m_works[ii].result = ERR_UNKNOWN;
			m_works[ii].test_id = -1;
			m_works[ii].seed = 0;

			m_works[ii].event_start = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			m_works[ii].event_complete = CreateEvent(nullptr, FALSE, TRUE, nullptr);

			m_thread_list[ii] = CreateThread(nullptr, 0, _RunTestQueue, (m_works + ii), 0, nullptr);
			if (m_thread_list[ii] == nullptr) THROW_WIN32_ERROR(L"faild on creating working thread %d", ii);
		}
	}

	srand(100);

	return 0;
}

void CExStatisticTester::FinishTest(void)
{
	// 让所有线程结束
	if (m_thread_num > 1) {
		LOG_DEBUG(L"waiting for all thread exit");
		DWORD ir = WaitForMultipleObjects(m_thread_num, m_thread_list, TRUE, 1000000);
		if (ir >= WAIT_TIMEOUT) {
			LOG_WIN32_ERROR(L"Thread cannot exit");
		}

		LOG_DEBUG(L"all thread exited.");
		for (UINT ii = 0; ii < m_thread_num; ++ii)
		{
			CloseHandle(m_works[ii].event_start);
			CloseHandle(m_works[ii].event_complete);
			CloseHandle(m_thread_list[ii]);
		}
	}
	delete[] m_thread_list;

	DeleteCriticalSection(&m_trace_crit);
}

bool CExStatisticTester::PrintProgress(INT64 ts)
{
	bool health_valid = false;
	HANDLE handle = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS_EX pmc = { 0 };
	GetProcessMemoryInfo(handle, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
	float mem_cost = (float)(pmc.WorkingSetSize / (1024.0 * 1024.0));	//MB
	if (mem_cost > m_max_memory_cost) m_max_memory_cost = mem_cost;

	// 输出到concole
	wprintf_s(L"ts=%llds, op=%d, testing=%d, completed=%d, failed=%d, mem=%.1fMB, max_mem=%.1fMB\n",
		ts, m_op_sn, m_testing, m_tested, m_failed, mem_cost, m_max_memory_cost);
//	wprintf_s(L"files=%d, logic=%d, phisic=%d, total=%d, free=%d, host write=%lld, media write=%lld\n",
//		m_file_num, m_logical_blks, m_physical_blks, m_total_blks, m_free_blks, m_host_write, m_media_write);
	return true;
}

void CExStatisticTester::GetTestSummary(boost::property_tree::wptree& sum)
{
	CExTester::GetTestSummary(sum);
	sum.add(L"test_cycle", m_tested);
	sum.add(L"failed", m_failed);
}

ERROR_CODE CExStatisticTester::RunTest(void)
{
	printf_s("Running STATISTIC test\n");
	if (m_thread_num <= 1) return SingleThreadTest();
	// for multithread
	CFsState init_state;
	init_state.m_real_fs = m_fs_factory;
	m_fs_factory->add_ref();
	
	m_tested = 0;
	m_failed = 0;
	m_testing = 0;
	LONG test = 0;

	HANDLE* complete_events = new HANDLE[m_thread_num];
	for (UINT ii = 0; ii < m_thread_num; ++ii) complete_events[ii] = m_works[ii].event_complete;

	while (1)
	{
		// 等待完成或者释放线程
		DWORD ir = WaitForMultipleObjects(m_thread_num, complete_events, FALSE, INFINITE);
		if (ir >= (0 + m_thread_num)) THROW_WIN32_ERROR(L"failed on waiting work");
		WORK_CONTEXT* work = m_works + ir;
		if (work->test_id != (UINT)-1)
		{			// 检查和处理测试结果
			m_tested++;
			if (work->result != ERR_OK) m_failed++;
			LOG_DEBUG(L"got result, id=%d, tested=%d", work->test_id, m_tested);
		}

		if (test < m_test_times)
		{	// 发送测试请求
			work->test_id = test;
			work->src_state = &init_state;
			work->result = ERR_UNKNOWN;
			SetEvent(work->event_start);
			test++;
		}
		else
		{	// 发送空请求，使测试线程结束
			work->test_id = (UINT)(-1);
			SetEvent(work->event_start);
		}
		if (m_tested >= m_test_times) {
			LOG_DEBUG(L"test completed, tested=%d", m_tested);
			break;
		}
	}
	delete[] complete_events;
	return ERR_OK;
}
ERROR_CODE CExStatisticTester::SingleThreadTest(void)
{
	CFsState init_state;
	init_state.m_real_fs = m_fs_factory;
	m_fs_factory->add_ref();

	m_tested = 0;
	m_failed = 0;
	m_testing = 0;
	LONG test = 0;
	TRACE_ENTRY ops[MAX_WORK_NR];

	CStateManager states;
	states.Initialize(0, false);
	for (; test < m_test_times; test++) {
		DWORD tid = GetCurrentThreadId();
		srand(tid + test);
		CFsState* ss = states.get();
		IFsSimulator* fs = nullptr;
		init_state.m_real_fs->Clone(fs);
		ss->Initialize("\\", fs);

		ERROR_CODE ir = OneTest(ss, test, tid + test, ops, &states);
		states.put(ss);
	}
	return ERROR_CODE();
}


#if 0
DWORD CExStatisticTester::RunTestQueue(void)
{
	// 初始化随机数
	CStateManager states;
	states.Initialize(0, false);
	IFsSimulator* fs = nullptr;
	m_fs_factory->Clone(fs);
	TRACE_ENTRY ops[MAX_WORK_NR];

	DWORD tid = GetCurrentThreadId();
	srand(tid);
	bool exit = false;
	while (1)
	{
		LOG_STACK_PERFORM(L"_p0")
		DWORD ir = WaitForSingleObject(m_sub_doorbell, INFINITE);
		if (ir != 0) THROW_WIN32_ERROR(L"failed on waiting submit doorbell");
		EnterCriticalSection(&m_sub_crit);
		if (m_sub_q.empty()) {// 当收到信号，且队列为空，表示测试完成，推出线程。
			LeaveCriticalSection(&m_sub_crit);
			LOG_DEBUG(L"signal of quit test queue");
			break;
		}
		WORK_CONTEXT* context = m_sub_q.front();
		m_sub_q.pop_front();
		LeaveCriticalSection(&m_sub_crit);
		LOG_DEBUG(L"pop new test, id=%d", context->test_id);
		CFsState* src_state = context->src_state;

		CFsState* init_state = states.get();
//		IFsSimulator* fs = nullptr;
		// 此处的m_real_fs是m_fs_factory;
		fs->CopyFrom(src_state->m_real_fs);
//		src_state->m_real_fs->Clone(fs);
		init_state->Initialize("\\", fs);
		fs->add_ref();
		int seed = tid + rand();

//		printf_s("Start Test, test_id=%d, thread_id=%d\n", context->test_id, tid);
		context->result = OneTest(init_state, context->test_id, seed, ops, &states);
//		printf_s("Complete Test, test_id=%d, thread_id=%d\n", context->test_id, tid);

		states.put(init_state);
//		printf_s("thread=%d, state.free=%zd, state.buffer=%zd\n", tid, states.m_free_nr, states.m_buffer_size);
		LOG_DEBUG(L"push test result, id=%d", context->test_id);
		EnterCriticalSection(&m_cmp_crit);
		m_cmp_q.push_back(context);
		LeaveCriticalSection(&m_cmp_crit);
		ReleaseSemaphore(m_cmp_doorbell, 1, nullptr);
	}
	fs->release();
	LOG_DEBUG(L"Exiting test thread, tid=%d", tid);
	return 0;
}
#else

DWORD CExStatisticTester::RunTestQueue(WORK_CONTEXT* work)
{
	// 初始化随机数
	CStateManager states;
	states.Initialize(15000, false);
	IFsSimulator* fs = nullptr;
	m_fs_factory->Clone(fs);
	TRACE_ENTRY ops[MAX_WORK_NR];

	DWORD tid = GetCurrentThreadId();
	srand(tid);
	bool exit = false;
	while (1)
	{
		LOG_STACK_PERFORM(L"_p0")
		DWORD ir = WaitForSingleObject(work->event_start, INFINITE);
		if (ir != 0) THROW_WIN32_ERROR(L"failed on waiting start, tid=%d", tid);
		if (work->test_id == (UINT)-1) {
			LOG_DEBUG(L"signal of quit test queue");
			break;
		}
		LOG_DEBUG(L"pop new test, id=%d", work->test_id);
		CFsState* src_state = work->src_state;

		CFsState* init_state = states.get();
		fs->CopyFrom(src_state->m_real_fs);
		init_state->Initialize("\\", fs);
		fs->add_ref();
		int seed = tid + rand();
		work->result = OneTest(init_state, work->test_id, seed, ops, &states);
		states.put(init_state);
		LOG_DEBUG(L"push test result, id=%d", work->test_id);
		SetEvent(work->event_complete);
	}
	fs->release();
	LOG_DEBUG(L"Exiting test thread, tid=%d", tid);
	return 0;

}

#endif

size_t CExStatisticTester::GenerateOps(CFsState* cur_state, TRACE_ENTRY* ops, size_t op_size)
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
		if (io_nr > 0) {
			int rr = rand();
			UINT rollback = rr % io_nr;
			AddOp(ops, op_size, index, OP_POWER_OFF_RECOVER, rollback);

		}
	}
	return index;
}


ERROR_CODE CExStatisticTester::OneTest(CFsState* src_state, DWORD test_id, int seed, TRACE_ENTRY* ops, CStateManager* states)
{
	char str[512];

	if (states == nullptr) states = &m_states;
	CFsState* cur_state = states->duplicate(src_state);
	srand(seed);
	DWORD tid = GetCurrentThreadId();
	int depth = 0;

	ERROR_CODE ir = ERR_OK;
	std::string err_msg = "Succeeded";
//	LOG_STACK_PERFORM(L"_p1");
	InterlockedIncrement(&m_testing);

	// 创建参数输出文件
	FILE* param_out = nullptr;
	sprintf_s(str, "%S\\param_out_%d.csv", m_log_path.c_str(), test_id);
	fopen_s(&param_out, str, "w+");
	if (param_out == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %S", str);
	fprintf_s(param_out, "step,op_code,op,dir_nr,file_nr,mount_cnt,usafe_mount_cnt,log_sat,phy_sat,free_blks,free_segs,host_write,media_write,waf\n");
	cur_state->m_unsafe_mount_cnt = 0;

	for (depth = 0; depth < m_test_depth; )
	{
		size_t op_nr = GenerateOps(cur_state, ops, MAX_WORK_NR);
		// 返回的op_nr 可能比ops.size()小。
		// 从可能的操作中随机选择一个操作，并执行
		int rr = rand();
		int index = rr % op_nr;
		CFsState* next_state = states->get();
		next_state->DuplicateWithoutFs(cur_state);
		next_state->m_op = ops[index];
		next_state->m_op.op_sn = m_op_sn++;
		states->put(cur_state);
		cur_state = next_state;

		try
		{
			UINT start_io_nr = cur_state->m_real_fs->GetCacheNum();
			// 常规测试
			ir = FsOperatorCore(cur_state, cur_state->m_op);
			if (ir == ERR_NO_OPERATION) continue;

			if (m_check_power_loss) {
				if (cur_state->m_op.op_code == OP_POWER_OFF_RECOVER) {
					LOG_DEBUG(L"rollback by power outage, org start=%d, rollback=%d", start_io_nr, cur_state->m_op.rollback);
					start_io_nr -= cur_state->m_op.rollback;
				}
				UINT end_io_nr = cur_state->m_real_fs->GetCacheNum();
				// power outage测试
				UINT io_count = end_io_nr - start_io_nr;
				LOG_DEBUG(L"start io nr=%d, end io nr=%d, io count=%d", start_io_nr, end_io_nr, io_count);
				JCASSERT(end_io_nr >= start_io_nr);
				for (UINT ii = 0; ii < io_count; ++ii)
				{
					CFsState* p_state = states->get();
					p_state->DuplicateFrom(cur_state);
					ERROR_CODE err = TestPowerOutage(p_state, ii);

					if (err > ERR_NO_SPACE) {
						ir = err;
						states->put(cur_state);
						cur_state = p_state;
						break;
					}
					states->put(p_state);
				}
			}
		}
		catch (jcvos::CJCException& err)
		{
			err_msg = err.what();
			Op2String(str, cur_state->m_op);
			printf_s("Test Failed: test id=%d, thread id=%d, step=%d, op=%s\n", test_id, tid, depth, str);
			printf_s("\t reason: % s\n", err_msg.c_str());
			CFsException* fs_err = dynamic_cast<CFsException*>(&err);
			if (fs_err) ir = fs_err->get_error_code();
			else 		ir = ERR_GENERAL;
			break;
		}

		if (ir > ERR_NO_SPACE) { break; }
		UpdateFsParam(cur_state->m_real_fs);
		OutputTestParameter(param_out, depth, cur_state);
		depth++;
	}

	fclose(param_out);
	if (ir <= ERR_NO_SPACE) ir = ERR_OK;
	// out error message
	sprintf_s(str, "[seed=%d], [depth=%d], %s", seed, depth, err_msg.c_str());
	OutputTrace_Thread(cur_state, ir, str, test_id);
	states->put(cur_state);

	//	states->put(init_state);
	InterlockedDecrement(&m_testing);
	return ir;
}

void CExStatisticTester::OutputTestParameter(FILE* param_out, int depth, CFsState * state)
{
	fprintf_s(param_out, "%d,", depth);		// step;
	fprintf_s(param_out, "%d,%s,", state->m_op.op_code, OpName(state->m_op.op_code));
	fprintf_s(param_out, "%d,%d,", state->m_ref_fs.m_dir_num, state->m_ref_fs.m_file_num);
	fprintf_s(param_out, "%d,%d,", state->m_ref_fs.m_reset_count, state->m_unsafe_mount_cnt);
	FS_INFO fs_info;
	state->m_real_fs->GetFsInfo(fs_info);
	//FsHealthInfo health_info;
	//state->m_real_fs->GetHealthInfo(health_info);

	fprintf_s(param_out, "%d,%d,%d,%d,", fs_info.used_blk, fs_info.used_seg*BLOCK_PER_SEG, fs_info.free_blk, fs_info.free_seg);
	fprintf_s(param_out, "%lld,%lld,%.2f\n", fs_info.host_write, fs_info.media_write, (float)(fs_info.media_write)/(fs_info.host_write));
}



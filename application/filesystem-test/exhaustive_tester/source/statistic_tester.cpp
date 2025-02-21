///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"
#include <Psapi.h>


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
	//InitializeCriticalSection(&m_sub_crit);
	//InitializeCriticalSection(&m_cmp_crit);
	//m_sub_doorbell = CreateSemaphore(nullptr, 0, MAX_WORK_NR, nullptr);
	//m_cmp_doorbell = CreateSemaphore(nullptr, 0, MAX_WORK_NR, nullptr);
	m_thread_list = new HANDLE[m_thread_num];

	//for (UINT ii = 0; ii < MAX_WORK_NR; ++ii)
	//{
	//	m_works[ii].tester = this;
	//	m_works[ii].state = nullptr;
	//	m_works[ii].src_state = nullptr;
	//	m_works[ii].result = ERR_UNKNOWN;
	//	m_works[ii].test_id = -1;
	//	m_works[ii].seed = 0;
	//	// 将work context放入complete queue备用
	//	m_cmp_q.push_back(m_works + ii);
	//	ReleaseSemaphore(m_cmp_doorbell, 1, nullptr);
	//}
	// 每个work对应一个thread，初始化work
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

		m_thread_list[ii] = CreateThread(nullptr, 0, _RunTestQueue, (m_works+ii), 0, nullptr);
		if (m_thread_list[ii] == nullptr) THROW_WIN32_ERROR(L"faild on creating working thread %d", ii);
	}

	srand(100);

	return 0;
}

void CExStatisticTester::FinishTest(void)
{
//	DestroyThreadpoolEnvironment(&m_tp_environ);

	// 让所有线程结束
	LOG_DEBUG(L"waiting for all thread exit");
//	ReleaseSemaphore(m_sub_doorbell, m_thread_num, nullptr);
	DWORD ir = WaitForMultipleObjects(m_thread_num, m_thread_list, TRUE, 1000000);
	if (ir >= WAIT_TIMEOUT) {
//		THROW_WIN32_ERROR(L"Thread cannot exit");
		LOG_WIN32_ERROR(L"Thread cannot exit");
	}

	LOG_DEBUG(L"all thread exited.");

	//CloseHandle(m_sub_doorbell);
	//CloseHandle(m_cmp_doorbell);
	//DeleteCriticalSection(&m_sub_crit);
	//DeleteCriticalSection(&m_cmp_crit);
	for (UINT ii = 0; ii < m_thread_num; ++ii)
	{
		CloseHandle(m_works[ii].event_start);
		CloseHandle(m_works[ii].event_complete);
		CloseHandle(m_thread_list[ii]);
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
	CFsState init_state;
	init_state.m_real_fs = m_fs_factory;
	m_fs_factory->add_ref();
	
	m_tested = 0;
	m_failed = 0;
	m_testing = 0;
	LONG test = 0;

	HANDLE* complete_events = new HANDLE[m_thread_num];
	for (UINT ii = 0; ii < m_thread_num; ++ii) complete_events[ii] = m_works[ii].event_complete;

//	if (m_thread_num > 1) {
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


/*
			DWORD ir = WaitForSingleObject(m_cmp_doorbell, INFINITE);
			if (ir != 0) THROW_WIN32_ERROR(L"failed on waiting completion doorbell");
			// 取出Queue
			EnterCriticalSection(&m_cmp_crit);
			WORK_CONTEXT* work = m_cmp_q.front();
			m_cmp_q.pop_front();
			LeaveCriticalSection(&m_cmp_crit);
			if (work->test_id != (UINT)-1)
			{	// 处理结果
				m_tested++;
				if (work->result != ERR_OK) m_failed++;
				LOG_DEBUG(L"got result, id=%d, tested=%d", work->test_id, m_tested);
			}
			if (m_tested >= m_test_times) {
				LOG_DEBUG(L"test completed, tested=%d", m_tested);
				break;
			}

			// 准备新的测试
			if (test < m_test_times)
			{
				work->test_id = test;
				work->src_state = &init_state;
				work->result = ERR_UNKNOWN;
				LOG_DEBUG(L"push new test, id=%d", test);
				EnterCriticalSection(&m_sub_crit);
				m_sub_q.push_back(work);
				LeaveCriticalSection(&m_sub_crit);
				ReleaseSemaphore(m_sub_doorbell, 1, nullptr);
				test++;
			}
*/
		}
//	}
	//else {
	//	CStateManager states;
	//	states.Initialize(0, false);
	//	for (; test < m_test_times; test++) {
	//		DWORD tid = GetCurrentThreadId();
	//		srand(tid+test);
	//		CFsState* ss = states.get();
	//		IFsSimulator* fs = nullptr;
	//		init_state.m_real_fs->Clone(fs);
	//		ss->Initialize("\\", fs);

	//		ERROR_CODE ir = OneTest(ss, test, tid+test, &states);
	//	}
	//}
		delete[] complete_events;
	return ERR_OK;
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


ERROR_CODE CExStatisticTester::OneTest(CFsState* src_state, DWORD test_id, int seed, TRACE_ENTRY* ops, CStateManager* states)
{

	//LOG_STACK_TRACE();
	//wprintf_s(L"START NEW TEST: tid=%d", test_id);
		if (states == nullptr) states = &m_states;
		CFsState* cur_state = states->duplicate(src_state);
		srand(seed);
		DWORD tid = GetCurrentThreadId();
		int depth = 0;

		ERROR_CODE ir = ERR_OK;
		std::string err_msg = "Succeeded";
	{
		LOG_STACK_PERFORM(L"_p1");
		//	CFsState* cur_state = init_state;
		//	std::vector<TRACE_ENTRY> ops;
//		TRACE_ENTRY ops[MAX_WORK_NR];
		InterlockedIncrement(&m_testing);
		for (depth = 0; depth < m_test_depth; depth++)
		{
			size_t op_nr = GenerateOps(cur_state, ops, MAX_WORK_NR);
			// 返回的op_nr 可能比ops.size()小。
			// 从可能的操作中随机选择一个操作，并执行
			int rr = rand();
//			int rr = depth;
			int index = rr % op_nr;

#if 1
			CFsState* next_state = states->duplicate(cur_state);
			next_state->m_op = ops[index];
			next_state->m_op.op_sn = m_op_sn++;
//			LOG_DEBUG_(1, L"setp=%d, available op=%d, random=%d, index=%d", depth, op_nr, rr, index);
			states->put(cur_state);
			cur_state = next_state;
#endif

			try
			{
				ir = FsOperatorCore(cur_state, cur_state->m_op);
//				ir = ERR_OK;
			}
			catch (jcvos::CJCException& err)
			{
				err_msg = err.what();
				char str[256];
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
		}
	}
//	return ir;

	{
		LOG_STACK_PERFORM(L"_p2");

		if (ir <= ERR_NO_SPACE) ir = ERR_OK;
		//	printf_s("Output trace, test_id=%d, thread_id=%d\n", test_id, tid);
			// out error message
		char str[512];
		sprintf_s(str, "[seed=%d], [depth=%d], %s", seed, depth, err_msg.c_str());
		OutputTrace_Thread(cur_state, ir, str, test_id);
		states->put(cur_state);

		//	states->put(init_state);
		InterlockedDecrement(&m_testing);
	}
	return ir;
}



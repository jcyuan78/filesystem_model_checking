///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/multithread-tester.h"

LOCAL_LOGGER_ENABLE(L"multithread", LOGGER_LEVEL_DEBUGINFO);


int CMultiThreadTest::PrepareTest(void)
{
	int err = 0;
	err = MakeFs();
	if (err) THROW_ERROR(ERR_APP, L"failed on make file system");

	if (m_need_mount)
	{
		err = MountFs();
		if (err) THROW_ERROR(ERR_APP, L"failed on mount fs during preparing");
	}

	m_test_state[0].Initialize(m_root);
	EnumerateOp(m_test_state[0]);
	m_op_sn = 0;

	// create working threads
	//m_threads = new HANDLE[m_thread_nr];
	//m_thread_ids = new DWORD[m_thread_nr];
	//for (size_t ii = 0; ii < m_thread_nr; ++ii)
	//{
	//	m_threads[ii] = CreateThread(NULL, 0, _Worker, this, 0, m_thread_ids + ii);
	//	if (m_threads[ii] == nullptr) THROW_WIN32_ERROR(L"failed on creating thread, id=%lld", ii);
	//}

//	m_requests = new std::list<OperateRequest*>[m_thread_nr];
	m_requests = new OperateRequest[m_thread_nr];
	m_works = new PTP_WORK[m_thread_nr];

	InitializeThreadpoolEnvironment(&m_callback_evn);
	m_pool = CreateThreadpool(NULL);
	SetThreadpoolThreadMaximum(m_pool, m_thread_nr);
	SetThreadpoolCallbackPool(&m_callback_evn, m_pool);


	return err;
}

int CMultiThreadTest::RunTest(void)
{
	UINT32 show_msg = 0;
	boost::posix_time::ptime ts_update = boost::posix_time::microsec_clock::local_time();;
	int err = 0;

	// initlaize state
	CTestState* cur_state = m_test_state;
	m_cur_depth = 0;
	while (1)
	{
		// pickup the first op in the 
		if (cur_state->m_cur_op >= cur_state->m_ops.size())
		{	// rollback, 需要逆向操作一下
			cur_state->m_cur_op = 0;	// 使当前状态无效

			if (m_cur_depth <= 0) break;
			m_cur_depth--;
			cur_state = m_test_state + m_cur_depth;
			JCASSERT(cur_state->m_cur_op > 0);
			FS_OP& op = cur_state->m_ops[cur_state->m_cur_op - 1];
			Rollback(cur_state->m_ref_fs, &op);
			continue;
		}

		// test
		int workers = 0;
		CTestState* next_state = cur_state + 1;
		next_state->Initialize(&cur_state->m_ref_fs);
		for (size_t ii = 0; ii < m_thread_nr; ++ii)
		{
			// 理想状态时从n个可行的操作中，选出所有m个组合，m为线程数量。
			if (cur_state->m_cur_op >= cur_state->m_ops.size()) break;

			OperateRequest *req = m_requests + ii;
			req->m_state = next_state;
			req->m_tester = this;
			req->m_op = & cur_state->m_ops[cur_state->m_cur_op];
			req->m_op->op_sn = m_op_sn;

			m_works[ii] = CreateThreadpoolWork(AsyncFsOperate, req, &m_callback_evn);
			SubmitThreadpoolWork(m_works[ii]);
			workers++;

			cur_state->m_cur_op++;
		}

		for (int ii = 0; ii < workers; ++ii)
		{
			WaitForThreadpoolWorkCallbacks(m_works[ii], FALSE);
		}

		// generate new state
//		op.op_sn = m_op_sn;
		//err = FsOperate(next_state->m_ref_fs, &op);
		//if (err)
		//{
		//	THROW_ERROR(ERR_USER, L"failed on call operations, err=%d", err);
		//}
		// 操作成功，保存操作，搜索下一步。
		// check max m_cur_depth
		if (m_cur_depth >= m_test_depth)
		{	// 达到最大深度，检查结果
			err = Verify(next_state->m_ref_fs);
			if (err)
			{
				TEST_ERROR(L"failed on verify fs, err=%d", err);
				LOG_ERROR(L"[err] failed on verify fs, err=%d", err);
				break;
			}
//			Rollback(cur_state->m_ref_fs, &op);
			size_t ss = next_state->m_history.size_s();
			for (size_t ii = 0; ii < ss; ++ii)
			{
				Rollback(cur_state->m_ref_fs, &next_state->m_history.at_s(ii));
			}
			continue;
		}
		else
		{	// enumlate ops for new state
			m_cur_depth++;
			EnumerateOp(*next_state);
			cur_state = next_state;
		}
		SetEvent(m_monitor_event);
	}

	TEST_LOG(L"\n\n=== result ===\n");
	TEST_LOG(L"  Test successed!\n");
	boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
	INT64 ts = (ts_cur - m_ts_start).total_seconds();
	PrintProgress(ts);
	wprintf_s(L"Test completed\n");
	return true;
}

int CMultiThreadTest::FinishTest(void)
{
	CloseThreadpool(m_pool);
	return CFullTester::FinishTest();
}

DWORD CMultiThreadTest::Worker(void)
{

	return 0;
}

void CMultiThreadTest::AsyncFsOperate(PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_WORK work)
{
	OperateRequest* req = reinterpret_cast<OperateRequest*>(context);
	req->m_state->m_history.push_back_s(*(req->m_op));
	int err = req->m_tester->FsOperate(req->m_state->m_ref_fs, req->m_op);


}

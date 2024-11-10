///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"

LOCAL_LOGGER_ENABLE(L"extester", LOGGER_LEVEL_DEBUGINFO);


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
	m_test_times = config.get<int>(L"test_times",10);
	return 0;
}

int CExStatisticTester::PreTest(void)
{
	m_cur_state = m_states.get();
	IFsSimulator* fs = nullptr;
	m_fs_factory->Clone(fs);

	m_cur_state->Initialize("\\", fs);
	m_cur_state->m_ref = 1;
//	m_open_list.push_front(init_state);
	InitializeCriticalSection(&m_trace_crit);

	srand(100);

	return 0;
}

ERROR_CODE CExStatisticTester::RunTest(void)
{
	printf_s("Running STATISTIC test\n");

	//for (size_t test = 0; test < m_test_times; ++test)
	//{

	//}
	return OneTest();

}

void CExStatisticTester::FinishTest(void)
{
	DeleteCriticalSection(&m_trace_crit);
}

ERROR_CODE CExStatisticTester::OneTest(void)
{
	ERROR_CODE ir = ERR_OK;
	std::string err_msg;

	std::vector<TRACE_ENTRY> ops;
	for (int depth = 0; depth < m_test_depth; depth++)
	{
		size_t op_nr = GenerateOps(m_cur_state, ops);
		// 返回的op_nr 可能比ops.size()小。
		// 从可能的操作中随机选择一个操作，并执行
		int rr = rand();
		int index = rr % op_nr;

		//		TRACE_ENTRY& op = ops[index];
		//		ERROR_CODE err = ERR_OK;
		CFsState* next_state = m_states.duplicate(m_cur_state);
		next_state->m_op = ops[index];
		next_state->m_op.op_sn = m_op_sn++;
		LOG_DEBUG(L"setp=%d, available op=%d, random=%d, index=%d", depth, op_nr, rr, index);
		m_cur_state = next_state;

		try
		{
			ir = FsOperatorCore(m_cur_state, m_cur_state->m_op);
		}
		catch (jcvos::CJCException& err)
		{
			err_msg = err.what();
			printf_s("Test failed with exception: %s\n", err_msg.c_str());
			ir = ERR_GENERAL;
		}

		if (ir != ERR_OK)
		{
			OutputTrace_Thread(m_cur_state, ir, err_msg);
			m_states.put(m_cur_state);
			return ir;
		}
		UpdateFsParam(m_cur_state->m_real_fs);
	}
	OutputTrace_Thread(m_cur_state, ir, "Succeeded");
	m_states.put(m_cur_state);
	return ERR_OK;
}



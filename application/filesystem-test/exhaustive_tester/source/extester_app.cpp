﻿///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "extester_app.h"
#include "../include/extester.h"
#include "../include/f2fs_simulator.h"
#include <boost/property_tree/json_parser.hpp>

#ifdef _DEBUG
#include <vld.h>
#endif

LOCAL_LOGGER_ENABLE(L"simulation.app", LOGGER_LEVEL_DEBUGINFO);

const TCHAR CExhaustiveTesterApp::LOG_CONFIG_FN[] = L"simulate.cfg";
typedef jcvos::CJCApp<CExhaustiveTesterApp>	CApplication;
CApplication _app;

#define _class_name_	CApplication

BEGIN_ARGU_DEF_TABLE()
ARGU_DEF(L"config", 'c', m_config_file, L"configuration file name")
ARGU_DEF(L"test_id", 't', m_test_id, L"test id for generating log and result")
ARGU_DEF(L"multihead", 'm', m_multihead_cnt, L"number of head count for mulithead log")
ARGU_DEF(L"depth", 'd', m_searching_depth, L"max depth for searching")
ARGU_DEF(L"thread", 'r', m_thread_num, L"thread number for searching")
ARGU_DEF(L"branch", 'b', m_branch, L"test branch for statistic check")
ARGU_DEF(L"type", 'p', m_test_type, L"test type: exhaustive, statistic, trace")
ARGU_DEF(L"trace", 0, m_trace_fn, L"trace filename for trace test")


//ARGU_DEF(L"target", 't', m_root, L"target folder to test, like D:, D:\\test")
END_ARGU_DEF_TABLE()

int _tmain(int argc, wchar_t* argv[])
{
	return jcvos::local_main(argc, argv);
}

CExhaustiveTesterApp::CExhaustiveTesterApp(void)
{
}

CExhaustiveTesterApp::~CExhaustiveTesterApp(void)
{
}

int CExhaustiveTesterApp::Initialize(void)
{
	//	EnableSrcFileParam('i');
	EnableDstFileParam('o');
	DWORD col = CJCLogger::Instance()->GetColumnSelect();
	col |= CJCLogger::COL_THREAD_ID;
	CJCLogger::Instance()->SetColumnSelect(col);
	return 0;
}

void CExhaustiveTesterApp::CleanUp(void)
{
}



void CExhaustiveTesterApp::MakeTestId(void)
{
	// 随机生成 test id
	time_t now;
	time(&now);
	tm ptm;
	localtime_s(&ptm, &now);
	m_test_id.resize(50);
	wchar_t* str_tid = const_cast<wchar_t*>(m_test_id.data());
	int len = swprintf_s(str_tid, 50, L"T%02d%02d%02d%02d%02d", ptm.tm_mon, ptm.tm_mday, ptm.tm_hour, ptm.tm_min, ptm.tm_sec);
	m_test_id.resize(len);
}

void CExhaustiveTesterApp::GenerateLogFileName(void)
{
	CreateDirectory(m_test_id.c_str(), nullptr);
	jcvos::auto_array<wchar_t> str_fn(MAX_PATH + 1);
	swprintf_s(str_fn, MAX_PATH, L"%s\\log.csv", m_test_id.c_str());
	m_fn_log = str_fn;
	swprintf_s(str_fn, MAX_PATH, L"%s\\lblk_a.csv", m_test_id.c_str());
	m_fn_lblka = str_fn;
	swprintf_s(str_fn, MAX_PATH, L"%s\\lblk_b.csv", m_test_id.c_str());
	m_fn_lblkb = str_fn;
	swprintf_s(str_fn, MAX_PATH, L"%s\\seg_a.csv", m_test_id.c_str());
	m_fn_seg = str_fn;
}

// 增加Ctrl+C的处理
BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{
	if (dwCtrlType == CTRL_C_EVENT)
	{
		CExhaustiveTesterApp* app = dynamic_cast<CExhaustiveTesterApp*>(CExhaustiveTesterApp::GetApp());
		app->StopTest();
		return TRUE;
	}
	return FALSE;
}

void CExhaustiveTesterApp::StopTest(void)
{
	if (m_tester) m_tester->StopTest();
}


int CExhaustiveTesterApp::Run(void)
{
	LOG_STACK_TRACE();

	if (m_test_id.empty()) MakeTestId();
	GenerateLogFileName();

	std::string default_config;
	CExTester* tester = nullptr;
	if (m_test_type == L"statistic")
	{
		tester = new CExStatisticTester;
		default_config = "config-statistic.xml";
	}
	else if (m_test_type == L"exhaustive")
	{
		tester = new CExTester;
		default_config = "config-exhaus.xml";
	}
	else if (m_test_type == L"trace")
	{
		tester = new CExTraceTester;
		default_config = "config-statistic.xml";
	}
	else
	{
		THROW_ERROR(ERR_USER, L"unknown test type: %s ", m_test_type);
	}


	// load config file
	std::string config_fn;
	if (m_config_file.empty())	config_fn = default_config;
	else 						jcvos::UnicodeToUtf8(config_fn, m_config_file);
	boost::property_tree::wptree prop;
	boost::property_tree::xml_parser::read_xml(config_fn, prop);

	boost::property_tree::wptree& test_config = prop.get_child(L"config.test");
	boost::property_tree::wptree& fs_config = prop.get_child(L"config.filesystem");

	// 参数覆盖配置文件
	if (m_multihead_cnt != 0)	{	fs_config.put(L"multi_header_num", m_multihead_cnt);	}
	if (m_searching_depth != 0)	{	test_config.put(L"depth", m_searching_depth);	}
	if (m_thread_num > 0)		{	test_config.put(L"thread_num", m_thread_num); }
	if (m_branch > 0)			{	test_config.put(L"branch", m_branch); }
	if (!m_trace_fn.empty())	{	test_config.put(L"trace", m_trace_fn); }

	if (m_test_type.empty() ) m_test_type = test_config.get<std::wstring>(L"type");
//	jcvos::auto_ptr<CF2fsSimulator> lfs(new CF2fsSimulator);
	IFsSimulator* lfs = CF2fsSimulator::factory();
	lfs->Initialzie(fs_config, m_test_id);

//	CExTester tester;
//	m_tester = tester;
	if (!SetConsoleCtrlHandler(HandlerRoutine, TRUE))
	{
		THROW_WIN32_ERROR(L"[err] failed on setting console ctrl handler");
	}
	tester->PrepareTest(test_config, lfs, m_test_id );
	tester->StartTest();

	tester->GetTestSummary(m_test_summary);
	// 保存测试的配置结果
	std::string str_log_fn;
	jcvos::UnicodeToUtf8(str_log_fn, m_test_id + L"\\config.xml");
	boost::property_tree::xml_parser::write_xml(str_log_fn, prop);
	// 保存测试结果小结
	jcvos::UnicodeToUtf8(str_log_fn, m_test_id + L"\\summary.json");
	boost::property_tree::json_parser::write_json(str_log_fn, m_test_summary);

	delete tester;

	lfs->release();

	m_tester = nullptr;
	return 0;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include <jcapp.h>

#include "../include/full_tester.h"
#include "../include/multithread-tester.h"
#include "../include/trace_tester.h"

#include <boost/property_tree/json_parser.hpp>

#ifdef _DEBUG
#include <vld.h>
#endif




class CFsTesterApp
	: public jcvos::CJCAppSupport<jcvos::AppArguSupport>
{
protected:
	typedef jcvos::CJCAppSupport<jcvos::AppArguSupport> BaseAppClass;

public:
	static const TCHAR LOG_CONFIG_FN[];
	CFsTesterApp(void);
	virtual ~CFsTesterApp(void);

public:
	virtual int Initialize(void);
	virtual int Run(void);
	virtual void CleanUp(void);
	virtual LPCTSTR AppDescription(void) const {
		return L"File System Tester, by Jingcheng Yuan\n";
	};


// app 参数
public:
	std::wstring m_config_file;
	std::wstring m_root;
};


LOCAL_LOGGER_ENABLE(L"fstester.app", LOGGER_LEVEL_DEBUGINFO);

const TCHAR CFsTesterApp::LOG_CONFIG_FN[] = L"fstester.cfg";
typedef jcvos::CJCApp<CFsTesterApp>	CApplication;
CApplication _app;

#define _class_name_	CApplication

BEGIN_ARGU_DEF_TABLE()
ARGU_DEF(L"config", 'c', m_config_file, L"configuration file name")
ARGU_DEF(L"target", 't', m_root,		L"target folder to test, like D:, D:\\test")
END_ARGU_DEF_TABLE()

int _tmain(int argc, wchar_t* argv[])
{
	return jcvos::local_main(argc, argv);
}

CFsTesterApp::CFsTesterApp(void)
{
}

CFsTesterApp::~CFsTesterApp(void)
{
}

int CFsTesterApp::Initialize(void)
{
	//	EnableSrcFileParam('i');
	EnableDstFileParam('o');
	return 0;
}

void CFsTesterApp::CleanUp(void)
{
}

int CFsTesterApp::Run(void)
{
	CTesterBase *tester= nullptr;

	// loading config
	std::string str_config_fn;
	jcvos::UnicodeToUtf8(str_config_fn, m_config_file);
	boost::property_tree::wptree pt_config;
	boost::property_tree::read_json(str_config_fn, pt_config);
	const boost::property_tree::wptree& test_config = pt_config.get_child(L"test");

	const std::wstring& test_type = test_config.get<std::wstring>(L"type", L"");
	if (test_type == L"full") { tester = new CFullTester; }
	else if (test_type == L"full_multi_thread") { tester = new CMultiThreadTest; }
	else if (test_type == L"trace_test") { tester = new CTraceTester; }

	if (tester == nullptr)
	{
		THROW_ERROR(ERR_MEM, L"memory full or unknown test type=%s", test_type.c_str());
	}

	// prepare for test
	tester->Config(test_config, m_root);
//	if (!m_root.empty()) tester->SetTestRoot(m_root);
	// runtest
	int err = tester->StartTest();

	delete tester;
	return err;
}
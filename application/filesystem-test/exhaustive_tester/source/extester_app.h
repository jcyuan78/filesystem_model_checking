///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <jcapp.h>
#include <boost/property_tree/xml_parser.hpp>


class CExhaustiveTesterApp
	: public jcvos::CJCAppSupport<jcvos::AppArguSupport>
{
protected:
	typedef jcvos::CJCAppSupport<jcvos::AppArguSupport> BaseAppClass;

public:
	static const TCHAR LOG_CONFIG_FN[];
	CExhaustiveTesterApp(void);
	virtual ~CExhaustiveTesterApp(void);

public:
	virtual int Initialize(void);
	virtual int Run(void);
	virtual void CleanUp(void);
	virtual LPCTSTR AppDescription(void) const {
		return L"File System Tester, by Jingcheng Yuan\n";
	};

protected:
	void MakeTestId(void);
	void GenerateLogFileName(void);
	void SsdTest(const boost::property_tree::wptree& config);
//	void FsTest(const boost::property_tree::wptree& config, CLfsInterface* lfs);

	// app ²ÎÊý
public:
	std::wstring m_config_file;
	std::wstring m_test_id;
	int m_multihead_cnt = 0;
	int m_searching_depth = 0;
	UINT m_thread_num = 0;

protected:
	std::wstring m_fn_log, m_fn_lblka, m_fn_lblkb, m_fn_seg;
	boost::property_tree::wptree m_test_summary;
};

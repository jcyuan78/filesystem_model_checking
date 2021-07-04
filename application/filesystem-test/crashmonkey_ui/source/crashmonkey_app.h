#pragma once

#include <jcapp.h>
#include <ifilesystem.h>

class CCrashMonkeyApp
	: public jcvos::CJCAppSupport<jcvos::AppArguSupport>
{
protected:
	typedef jcvos::CJCAppSupport<jcvos::AppArguSupport> BaseAppClass;

public:
	static const TCHAR LOG_CONFIG_FN[];
	CCrashMonkeyApp(void);
	virtual ~CCrashMonkeyApp(void);

public:
	virtual int Initialize(void);
	virtual int Run(void);
	virtual void CleanUp(void);
	virtual LPCTSTR AppDescription(void) const {
		return L"File System Tester, by Jingcheng Yuan\n";
	};

protected:
	bool LoadConfig(void);
	void LoadFactory(IFsFactory*& factory, const std::wstring & lib_name);

public:
	std::wstring m_config_file;
	std::wstring m_test_dev_name;
	std::wstring m_test_case_name;
	size_t m_test_dev_size;
	size_t m_disk_size;

protected:
//	std::wstring m_str_lib;

	IFsFactory * m_fs_factory;
	std::wstring m_fs_name;

	IFsFactory* m_dev_factory;
	std::wstring  m_dev_name;
	IVirtualDisk* m_test_dev;

	IFsFactory* m_tester_factory;
	std::wstring m_test_case;
};

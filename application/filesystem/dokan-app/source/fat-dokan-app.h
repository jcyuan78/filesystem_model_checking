#pragma once

#include <jcapp.h>
#include "../../dokanfs-lib/include/ifilesystem.h"


class CFatDokanApp
	: public jcvos::CJCAppSupport<jcvos::AppArguSupport>
{
protected:
	typedef jcvos::CJCAppSupport<jcvos::AppArguSupport> BaseAppClass;

public:
	static const TCHAR LOG_CONFIG_FN[];
	CFatDokanApp(void);
	virtual ~CFatDokanApp(void);

public:
	virtual int Initialize(void);
	virtual int Run(void);
	virtual void CleanUp(void);
	virtual LPCTSTR AppDescription(void) const {
		return _T("Logic Picture, by Jingcheng Yuan\n");
	};

protected:
	static DWORD WINAPI StartDokanThread(LPVOID lp);

public:
	void Demount(void);

protected:
	IVirtualDisk * m_dev;
	IFileSystem * m_fs;
	HANDLE m_thread_dokan;

public:
	std::wstring m_config_fn;
	std::wstring m_device;
	std::wstring m_mount;
	std::wstring m_volume_name;
	std::wstring m_str_lib;		// library / dll name
	std::wstring m_str_fs;		// file system name
	size_t m_capacity;		// in sectors
	bool m_unmount;
};

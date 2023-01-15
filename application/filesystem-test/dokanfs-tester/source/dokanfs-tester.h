#pragma once

#include <jcapp.h>
#include <dokanfs-lib.h>
//#include "../include/reference_fs.h"

#include <vector>

#define MAX_FILE_SIZE	8192
#define MAX_CHILD_NUM 3

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

// test functions
public:
	//bool GeneralTest(void);
	bool SporTest(void);
	//bool FullTest(const boost::property_tree::wptree & prop);

protected:
	//bool FsOperate(IFileSystem * fs, CReferenceFs & ref, const FS_OP * op);
	//bool TestMount(IFileSystem * fs, CReferenceFs & ref);
	//bool TestWrite(IFileSystem * fs, CReferenceFs & ref, bool overwrite, const std::wstring & path, size_t len);
	//bool TestPower(IFileSystem * fs, CReferenceFs & ref);

	//bool Rollback(IFileSystem * fs, CReferenceFs & ref, const FS_OP * op);
	DWORD AppendChecksum(DWORD cur_checksum, const char * buf, size_t size);
	//bool Verify(const CReferenceFs &ref, IFileSystem * fs);
	bool PrintProgress(INT64 ts);

	bool MakeDir(const std::wstring& dir_name);
	int  NewFile(const std::wstring& dir_name);

	int  DtDeleteFile(const std::wstring& dir_name);

	UINT m_op_id;

protected:
//	std::vector<CTestState> m_test_state;
//	CTestState	m_test_state[MAX_DEPTH + 10];
	wchar_t m_log_buf[1024];

protected:
	IVirtualDisk * m_dev;
	IFileSystem * m_fs;
	HANDLE m_thread_dokan;
	FILE * m_log_file;
	std::wstring m_str_lib;		// library / dll name
	std::wstring m_str_fs;		// file system name
	UINT32 m_total_block;

// test confiuration 
	bool m_test_spor;
	bool m_support_trunk;
public:
	std::wstring m_config_file;
	std::wstring m_save_config;
	//std::wstring m_mount;
	std::wstring m_volume_name;
	std::wstring m_log_fn;
	//bool m_unmount;
	//std::wstring m_root;

	size_t m_test_depth;
};

#pragma once

#include <jcapp.h>
#include <dokanfs-lib.h>
#include "reference_fs.h"

#include <vector>

#define MAX_FILE_SIZE	8192
#define MAX_CHILD_NUM 3

enum OP_ID
{
	OP_NONE, 
	CREATE_FILE, CREATE_DIR, 
	DELETE_FILE, DELETE_DIR, 
	MOVE, APPEND_FILE, OVER_WRITE, 
	DEMOUNT_MOUNT,
	POWER_OFF_RECOVERY,
};


class FS_OP
{
public:
	FS_OP(void) : op_id(OP_NONE) {};
	FS_OP(OP_ID id, const std::wstring & fn) : op_id(id), path(fn) {};
	FS_OP(OP_ID id, const std::wstring & fn, const std::wstring &p1) : op_id(id), path(fn), param1(p1) {};
public:
	OP_ID op_id;
	std::wstring path;
	std::wstring param1;
	std::wstring param2;
};
typedef FS_OP * PFSOP;

class CTestState
{
public:
	//FS_OP m_op;	// 通过什么操作获得此状态
	CReferenceFs m_ref_fs;
	std::vector<FS_OP> m_ops;
	UINT32 m_cur_op;

public:
	void Initialize(const CReferenceFs * src);
	bool EnumerateOp(bool test_spor);
	void OutputState(FILE * log_file);
protected:
	void GenerateFn(wchar_t * fn, size_t len);
};


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
	bool GeneralTest(void);
	bool SporTest(void);
	bool FullTest(void);

protected:
	bool FsOperate(IFileSystem * fs, CReferenceFs & ref, const FS_OP * op);
	bool TestMount(IFileSystem * fs, CReferenceFs & ref);
	bool TestWrite(IFileSystem * fs, CReferenceFs & ref, bool overwrite, const std::wstring & path, size_t len);
	bool TestPower(IFileSystem * fs, CReferenceFs & ref);

	bool Rollback(IFileSystem * fs, CReferenceFs & ref, const FS_OP * op);
	DWORD AppendChecksum(DWORD cur_checksum, const char * buf, size_t size);
	bool Verify(const CReferenceFs &ref, IFileSystem * fs);
	bool PrintProgress(INT64 ts);

#ifdef _DEBUG
	UINT m_op_id;
#endif 

protected:
//	std::vector<CTestState> m_test_state;
	CTestState	m_test_state[MAX_DEPTH + 10];
	wchar_t m_log_buf[1024];

protected:
	IVirtualDisk * m_dev;
	IFileSystem * m_fs;
	HANDLE m_thread_dokan;
	FILE * m_log_file;
	std::wstring m_str_lib;		// library / dll name
	std::wstring m_str_fs;		// file system name
	size_t m_capacity;		// in sectors
	UINT32 m_total_block;

// test confiuration 
	bool m_test_spor;
	bool m_support_trunk;
public:
	std::wstring m_config_file;
	std::wstring m_mount;
	std::wstring m_volume_name;
	std::wstring m_log_fn;
	bool m_unmount;

	size_t m_test_depth;


};

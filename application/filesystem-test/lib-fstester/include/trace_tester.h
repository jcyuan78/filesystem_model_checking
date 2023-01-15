///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "tester_base.h"
class CTraceTester;

class TRACE_INFO
{
public:
	std::vector<TRACE_ENTRY> m_trace;
	std::wstring m_trace_fn;
	HANDLE m_thread;
	CTraceTester* m_tester;
	DWORD m_tid;		// trace id，不是thread id
	size_t m_max_buf_size = 0;
	BYTE* m_buf = nullptr;
	size_t m_trace_nr=0;		// trace entry的数量
};

#define MAX_THREAD 10

class FILE_ACCESS_INFO
{
public:
	FILE_ACCESS_INFO(void)
	{
		InitializeSRWLock(&file_lock);
		memset(file_handle, 0, sizeof(HANDLE) * MAX_THREAD);
		memset(open_ref, 0, sizeof(UINT) * MAX_THREAD);
	}
	~FILE_ACCESS_INFO(void)
	{
	}
public:
	std::wstring	file_name;
	DWORD			fid =0;
	DWORD			parent_fid = 0;
	UINT64			max_length = 0;
	UINT64			total_read = 0;
	UINT64			total_write = 0;
	bool			is_dir;
	UINT			child_nr = 0;
	IFileInfo *		file_handle[MAX_THREAD];
	UINT			open_ref[MAX_THREAD];	//文件打开计数
	DWORD			revision = 1;	// 写入的版本号
	SRWLOCK			file_lock;		// 文件锁
};

class CTraceTester : public CTesterBase
{
public:
	CTraceTester(IFileSystem* fs, IVirtualDisk* disk);
	virtual ~CTraceTester(void);
protected:
	virtual void Config(const boost::property_tree::wptree& pt, const std::wstring& root);
	virtual int PrepareTest(void);
	virtual int RunTest(void);
	virtual int FinishTest(void);
	virtual void ShowTestFailure(FILE* log) {}

protected:
	void CalculateFileAccess(TRACE_ENTRY& op);
	DWORD NewFileInfo(TRACE_ENTRY& op);
	UINT64 WriteTest(const FILE_ACCESS_INFO& info, IFileInfo* file, BYTE * buf, size_t start, size_t len);
	UINT64 ReadTest(const FILE_ACCESS_INFO& info, IFileInfo * file, BYTE * buf, size_t start, size_t len);
	void ReserveFile(IFileInfo* file, const FILE_ACCESS_INFO& info);
	DWORD FindOrNewFile(const std::wstring& path, bool is_dir);
	void PrepareFiles(void);

	// 从fn读取trace, 添加到tid中。
	void LoadTrace(const std::wstring& fn, UINT tid);

	DWORD TestThread(TRACE_INFO& trace);
	static DWORD WINAPI _TestThread(PVOID p)
	{
		TRACE_INFO* trace = (TRACE_INFO*)p;
		return trace->m_tester->TestThread(*trace);
	}


protected:
	static OP_CODE StringToOpCode(const std::wstring& str);
	static OP_CODE StringToOpCode(const char* str);

protected:
	std::vector<TRACE_INFO> m_traces;

	std::map<std::wstring, DWORD>	m_path_map;
	DWORD m_max_fid = 1;
	std::vector<FILE_ACCESS_INFO> m_file_access;

	LONGLONG m_total_write_time = 0, m_total_read_time;

	static const size_t m_file_buf_size = 128 * 1024 * 1024;
	char* m_file_buf = nullptr;
};
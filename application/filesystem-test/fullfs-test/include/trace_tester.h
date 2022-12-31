///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "full_tester.h"

class TRACE_ENTRY
{
public:
	enum OP_CODE
	{
		OP_NOP,
		OP_THREAD_CREATE, OP_THREAD_EXIT,
		OP_FILE_CREATE, OP_FILE_CLOSE, OP_FILE_READ, OP_FILE_WRITE, OP_FILE_FLUSH,
		OP_CREATE_DIR, OP_FILE_OPEN,   OP_FILE_DELETE, OP_FILE_OVERWRITE,
	};

public:
	UINT64 ts;
	OP_CODE code;
	DWORD thread_id;
	DWORD fid;
	std::wstring file_path;
	UINT64 duration =0;	// 操作所用的时间
	union {
		struct {	// for thread
			DWORD new_thread_id;
		};
		struct {	// for create
			//OP_CODE mode;
			bool is_dir;
			bool is_async;
		};
		struct {	// for read write
			size_t offset;
			size_t length;
		};
	};
};

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
	//HANDLE* m_files = nullptr;
	//UINT* m_open_ref = nullptr;
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
	HANDLE			file_handle[MAX_THREAD];
	UINT			open_ref[MAX_THREAD];	//文件打开计数
	DWORD			revision = 1;	// 写入的版本号
	SRWLOCK			file_lock;		// 文件锁
};

class CTraceTester : public CTesterBase
{
public:
	CTraceTester(void);
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
	UINT64 WriteTest(const FILE_ACCESS_INFO& info, HANDLE file, BYTE * buf, size_t start, size_t len);
	UINT64 ReadTest(const FILE_ACCESS_INFO& info, HANDLE file, BYTE * buf, size_t start, size_t len);
	void FillFile(HANDLE file, const FILE_ACCESS_INFO & info, size_t start, size_t len);
	void ReserveFile(HANDLE file, const FILE_ACCESS_INFO& info);
	DWORD FindOrNewFile(const std::wstring& path, bool is_dir);

	// 从fn读取trace, 添加到tid中。
	void LoadTrace(const std::wstring& fn, UINT tid);

	DWORD TestThread(TRACE_INFO& trace);
	static DWORD WINAPI _TestThread(PVOID p)
	{
		TRACE_INFO* trace = (TRACE_INFO*)p;
		return trace->m_tester->TestThread(*trace);
	}


protected:
	static TRACE_ENTRY::OP_CODE StringToOpCode(const std::wstring& str);

protected:
	std::vector<TRACE_INFO> m_traces;

	std::map<std::wstring, DWORD>	m_path_map;
	DWORD m_max_fid = 1;
	std::vector<FILE_ACCESS_INFO> m_file_access;

	LONGLONG m_total_write_time = 0, m_total_read_time;
};
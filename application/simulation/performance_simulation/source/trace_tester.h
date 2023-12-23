///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

//#include "tester_base.h"

#include "lfs_simulator.h"

#include <vector>
#include <map>
#include <boost/property_tree/ptree.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#define MAX_LINE_BUF	(1024)


class CTraceTester;

enum OP_CODE
{
	OP_NOP, OP_NO_EFFECT,
	OP_THREAD_CREATE, OP_THREAD_EXIT,
	OP_FILE_CREATE, OP_FILE_CLOSE, OP_FILE_READ, OP_FILE_WRITE, OP_FILE_FLUSH,
	OP_FILE_OPEN, OP_FILE_DELETE, OP_FILE_OVERWRITE,
	OP_DIR_CREATE, OP_DIR_DELETE, OP_MOVE,
	OP_DEMOUNT_MOUNT, OP_POWER_OFF_RECOVER,
	OP_MARK_TRACE_BEGIN,
};

class TRACE_ENTRY
{
public:
	UINT64 ts;
	OP_CODE op_code = OP_CODE::OP_NOP;
	DWORD thread_id;
	size_t file_index;		// index不是id，是file access info中的索引
	//std::wstring file_path;
	UINT64 duration = 0;	// 操作所用的时间
	UINT op_sn;
	union {
		struct {	// for thread
			DWORD new_thread_id;
		};
		struct {	// for create, open, delete
			//OP_CODE mode;
			bool is_dir;
			bool is_async;
		};
		struct {	// for read write
			size_t offset;
			size_t length;
		};
		struct {
			WORD trace_id;
			WORD test_cycle;
		};
	};
};

enum FIELD_INDEX
{
	FIELD_TIMESTAMP = 0, FIELD_OPERATION = 1, FIELD_PATH = 2, FIELD_RESULT = 3, FIELD_TID = 4, FIELD_PARAM1 = 5,
	FIELD_PARAM2 = 6, FIELD_PARAM3 = 7,
};

class TRACE_INFO_BASE
{
public:
	virtual ~TRACE_INFO_BASE(void) {}
	virtual void Reset(void) = 0;			// 初始化
	virtual bool IsForever(void) = 0;
	virtual TRACE_ENTRY* NextOp(void) = 0;
	virtual void LoadTrace(const boost::property_tree::wptree& trace/*, CTraceTester * tester, int traceid*/) = 0;

public:
	DWORD m_tid;		// trace id，不是thread id
	CTraceTester* m_tester;
};

class TRACE_INFO : public TRACE_INFO_BASE
{
public:
	virtual ~TRACE_INFO(void) {}
	virtual void Reset(void) { m_next_op = m_trace.begin(), m_next_cycle = 0; }			// 初始化
	virtual bool IsForever(void) { return false; }
	virtual TRACE_ENTRY* NextOp(void);
	//{
	//	if (m_next_cycle >= m_repeat) return nullptr;
	//	if (m_next_op == m_trace.end())
	//	{
	//		if (m_next_cycle >= m_repeat) return nullptr;
	//		m_next_cycle++;
	//		m_next_op = m_trace.begin();
	//	}
	//	TRACE_ENTRY *op = &(*m_next_op);
	//	m_next_op++;
	//	return op;
	//}
	virtual void LoadTrace(const boost::property_tree::wptree& trace/*, CTraceTester* tester, int trace_id*/);

public:
	std::vector<TRACE_ENTRY> m_trace;
	std::wstring m_trace_fn;
	HANDLE m_thread;
	size_t m_trace_nr=0;		// trace entry的数量
	int m_repeat;	// 重复次数
	// for working
	std::vector<TRACE_ENTRY>::iterator m_next_op;
	int m_next_cycle = 0;
};

class TRACE_INFO_COLD_FILES :public TRACE_INFO_BASE
{
	virtual void Reset(void) {}			// 初始化
	virtual bool IsForever(void) {	return false; }
	virtual TRACE_ENTRY* NextOp(void) { return nullptr; }
	virtual void LoadTrace(const boost::property_tree::wptree& trace/*, CTraceTester* tester, int traceid*/);

};

class FILE_ACCESS_INFO;

class TRACE_INFO_HOT_FILES : public TRACE_INFO_BASE
{
protected:
	virtual void Reset(void) {}			// 初始化
	virtual bool IsForever(void) { return true; }
	virtual TRACE_ENTRY* NextOp(void);
	virtual void LoadTrace(const boost::property_tree::wptree& trace/*, CTraceTester* tester, int traceid*/);
protected:
	//std::vector<FILE_ACCESS_INFO> m_files;
	FILE_ACCESS_INFO* m_file = nullptr;
	size_t m_file_index;
	UINT m_secs;
	TRACE_ENTRY m_op;
};

#define MAX_THREAD 10

class FILE_ACCESS_INFO
{
public:
	FILE_ACCESS_INFO(void)
	{
		InitializeSRWLock(&file_lock);
		memset(open_ref, 0, sizeof(UINT) * MAX_THREAD);
	}
	~FILE_ACCESS_INFO(void)
	{
	}
public:
	std::wstring	file_name;
	FID				fid =0;
	size_t			parent_fid = 0;
	UINT64			max_length = 0;
	UINT64			total_read = 0;
	UINT64			total_write = 0;
	bool			is_dir;
	UINT			child_nr = 0;
//	IFileInfo *		file_handle[MAX_THREAD];
	UINT			open_ref[MAX_THREAD];	//文件打开计数
	DWORD			revision = 1;	// 写入的版本号
	SRWLOCK			file_lock;		// 文件锁
};

class CTraceTester /*: public CTesterBase*/
{
public:
	CTraceTester(CLfsInterface * lfs);
	virtual ~CTraceTester(void);
public:
	virtual void Config(const boost::property_tree::wptree& pt, const std::wstring& root);
	virtual int PrepareTest(void);
	virtual int RunTest(void);
	virtual int FinishTest(void);
	virtual void ShowTestFailure(FILE* log) {}
	int StartTest(void);
	void SetLogFolder(const std::wstring& fn);

public:


protected:
	UINT64 WriteTest(FILE_ACCESS_INFO& info, size_t start, size_t len);
	UINT64 ReadTest(const FILE_ACCESS_INFO& info, size_t start, size_t len);

	void ReserveFile(const FILE_ACCESS_INFO& info);

	void FillFile(FID fid, DWORD revision, size_t secs);


	size_t FindOrNewFile(const std::wstring& path, bool is_dir);
	void PrepareFiles(void);

	// 从fn读取trace, 添加到tid中。
//	void LoadTrace(const std::wstring& fn, UINT tid);
	void LoadTrace(const std::wstring& fn, TRACE_INFO & trace_info, int trace_id);

	DWORD TestThread(TRACE_INFO& trace);
	static DWORD WINAPI _TestThread(PVOID p)
	{
		TRACE_INFO* trace = (TRACE_INFO*)p;
		return trace->m_tester->TestThread(*trace);
	}
	void SizeToSector(size_t& lba, size_t& secs, size_t offset, size_t len);

	bool PrintProgress(INT64 ts);

	void InvokeOperateion(TRACE_ENTRY& op, DWORD tid);

public:
	static OP_CODE StringToOpCode(const std::wstring& str);
	static OP_CODE StringToOpCode(const char* str);
	static int CalculatePrefix(const char* path);
	size_t NewFileInfo(/*TRACE_ENTRY& op*/ const std::wstring&fn, bool is_dir);
	size_t AddFIleInfo(const std::wstring& fn, size_t size);
	FILE_ACCESS_INFO& GetFile(size_t index) { return m_file_access.at(index); }
	void CalculateFileAccess(TRACE_ENTRY& op);

protected:

	void DumpFileMap(int index);

protected:
	std::vector<TRACE_INFO_BASE*> m_traces;

	LONGLONG m_total_write_time = 0, m_total_read_time;

	static const size_t m_file_buf_size = 128 * 1024 * 1024;
	char* m_file_buf = nullptr;

	std::wstring m_root;
	FILE* m_log_invalid_trace = nullptr;
	std::wstring m_log_folder;
	wchar_t m_log_buf[1024];

	// 用于监控文件操作是否超时
	long m_running;
	DWORD m_timeout;
	DWORD m_update_ms = 30;		// log/屏幕更新时间（毫秒单位）
	HANDLE m_monitor_thread;
	HANDLE m_monitor_event;
	UINT m_op_sn;
	boost::posix_time::ptime m_ts_start;

	DWORD Monitor(void);
	static DWORD WINAPI _Monitor(PVOID p)
	{
		CTraceTester* tester = (CTraceTester*)p;
		return tester->Monitor();
	}

	// 文件名 到 m_file_access的索引，FID保存在m_file_access中。FID有可能不连续。
	std::map<std::wstring, size_t> m_path_map;
	std::vector<FILE_ACCESS_INFO> m_file_access;

	CLfsInterface* m_lfs;
	int m_repeat;	// 整个trace重复次数
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_simulator.h"
#include "reference_fs.h"
#include <list>
#include <boost/unordered_set.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum ERROR_CODE
{
	ERR_OK = 0,
	ERR_GENERAL,
	OK_ALREADY_EXIST,	// 文件或者目录已经存在，但是结果返回true
	ERR_CREATE_EXIST,	// 对于已经存在的文件，重复创建。
	ERR_CREATE,			// 文件或者目录不存在，但是创建失败
	ERR_OPEN_FILE,		// 试图打开一个已经存在的文件是出错
	ERR_GET_INFOMATION,	// 获取File Informaiton时出错
	ERR_DELETE_FILE,	// 删除文件时出错
	ERR_DELETE_DIR,		// 删除目录时出错
	ERR_READ_FILE,		// 读文件时出错
	ERR_WRONG_FILE_SIZE,	// 
	ERR_WRONG_FILE_DATA,
};

class CFsState
{
public:
	CFsState(void) {};
	~CFsState(void);
public:
	CReferenceFs m_ref_fs;
	IFsSimulator* m_real_fs = nullptr;
	TRACE_ENTRY m_op;		// 上一个操作如何执行到这一步
	int m_depth;			// 搜索深度

public:
	void Initialize(const std::wstring& root_path, IFsSimulator * fs)
	{
		m_ref_fs.Initialize(root_path);
		m_real_fs = fs;
	}
	void OutputState(FILE* log_file);
	void DuplicateFrom(CFsState* src_state);
};

class CStateManager
{
public:
	CStateManager(void);
	~CStateManager(void);

public:
	void Initialize(size_t size);
	CFsState* get(void);
	void put(CFsState* &state);
	CFsState* duplicate(CFsState* state);
protected:
	//std::vector<
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == State heap and hash code

struct ENCODE
{
	DWORD code[20];
	bool operator == (const ENCODE& e) const {
		return memcmp(code, e.code, 20) == 0;
	}
};

inline size_t hash_value(const ENCODE& e)
{
	size_t seed = 0;
	for (int ii = 0; ii < 20; ++ii)	boost::hash_combine(seed, e.code[ii]);
	return seed;
}

class CStateHeap
{
public:
	//比较state是否已经被检查过，如果是，则返回true，否者添加并返回false；
	//bool CheckAndInsert(const CFsState* state);
	bool Check(const CFsState* state);
	void Insert(const CFsState* state);
	size_t StateNr(void) const {
		return m_fs_state.size();
	}
	size_t size(void) { return m_fs_state.size(); }
protected:
	boost::unordered_set<ENCODE> m_fs_state;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== Exhaustive Tester
class CExTester // : public ITester
{
public:
	CExTester(void);
	virtual ~CExTester(void);

public:
	virtual int StartTest(void);
	virtual int PrepareTest(const boost::property_tree::wptree & config, IFsSimulator * fs, const std::wstring& log_path);
	int PreTest(void);
	int RunTest(void);
	void FinishTest(void);
	virtual void ShowTestFailure(FILE* log);
	virtual void GetTestSummary(boost::property_tree::wptree& sum);

protected:
	ERROR_CODE DoFsOperator(CFsState* cur_state, TRACE_ENTRY& op, std::list<CFsState*>::iterator& insert);

	//	int TestCreate(CReferenceFs& ref, const std::wstring & path, const std::wstring & fn, bool isdir);
	// 返回一个执行后的新的状态
	//CFsState * TestCreate(CFsState * cur_state, const std::wstring& path, bool isdir);

	ERROR_CODE TestCreateFile(CFsState* cur_state, const std::wstring& path);
	ERROR_CODE TestCreateDir (CFsState* cur_state, const std::wstring& path);
	ERROR_CODE TestWriteFile(CFsState * state, const std::wstring& path, FSIZE offset, FSIZE len);

	int TestDelete(CFsState* state, CReferenceFs& ref, const std::wstring& path);
	int TestMove(CFsState* state, CReferenceFs& ref, const std::wstring& path_src, const std::wstring& path_dst);
	//	int TestMount(CLfsInterface* fs, CReferenceFs& ref);

	//	bool TestPower(CLfsInterface* fs, CReferenceFs& ref);

	int Rollback(CReferenceFs& ref, const TRACE_ENTRY* op);
	//	DWORD AppendChecksum(DWORD cur_checksum, const char* buf, size_t size);
	ERROR_CODE Verify(CFsState* cur_state);

	// 针对每个子项，枚举所有可能的操作。
	bool EnumerateOp(CFsState * cur_state, std::list<CFsState*>::iterator & insert);

	// for monitor thread
	//DWORD Monitor(void);
	static DWORD WINAPI _RunTest(PVOID p);
	bool PrintProgress(INT64 ts);

protected:


protected:
	// make fs, mount fs, 和unmount fs是可选项。暂不支持。
	// make fs仅用于初始化。mount fs用于初始化和测试。测试中允许增加mount和unmount操作
	int MakeFs(void) { return 0; }
	int MountFs(void) { return 0; }
	int UnmountFs(void) { return 0; }

protected:
	// debug and monitor
	void ShowStack(CFsState* cur_state);

protected:
	// state 内存管理里
	//void InitStateBuf(size_t state_nr);
	//CFsState* get_state(void);
	//void put_state(CFsState* state);

protected:
	CStateManager m_states;
	std::list<CFsState*> m_open_list;
	CStateHeap m_closed;

protected:
	// 测试参数：最大深度，最大文件数等
	DWORD m_update_ms = 50000;		// log/屏幕更新时间（毫秒单位）
	size_t m_max_child_num;					// 目录下的最大子文件/子目录数量
	size_t m_max_dir_depth;					// 最大目录深度
	size_t m_max_file_op;					// 最大文件操作（写入）次数
	FSIZE m_max_file_size;					// 文件的最大尺寸
	int m_clear_temp;						// 测试完以后是否清除所有临时文件
	int m_test_depth;						// 最大测试深度
	size_t m_volume_size;					// 文件系统大小

	std::vector<OP_CODE> m_file_op_set;		// 对于文件，允许的操作
	std::vector<OP_CODE> m_dir_op_set;		// 对于目录，允许的操作
	std::vector<OP_CODE> m_fs_op_set;			// 对于文件系统，允许的操作，例如unmount/mount. spor等
	// 测试条件
	bool m_mount, m_format;		// 在测试前是否要format和mount fs
	DWORD m_timeout;

	// 测试运行状态
	UINT m_op_sn;
	boost::posix_time::ptime m_ts_start;
	DWORD m_test_thread_id;
	HANDLE m_test_thread;
	HANDLE m_test_event;

	float m_max_memory_cost = 0;		// 最大内存使用量
	LONG m_max_depth = 0;


	// log support
	std::wstring m_log_path;
	FILE* m_log_file = nullptr;
	wchar_t m_log_buf[1024];


	// 其他
	IFsSimulator* m_fs_factory = nullptr;		// 这里的fs作为文件系统初始状态，以及的factory，用于创建文件测试用的文件系统。

/*
	// 参数，选项
protected:
	//	bool m_need_mount = false;
	bool m_support_trunk = true;


	CStateHeap m_states;
	size_t m_state_searched = 0;
	size_t m_max_depth = 0;

protected:
	int m_cur_depth;						// 当前测试的深度

	CFsState* m_test_state_buf;
	std::list<CFsState*> m_free_list;
	std::list<CFsState*> m_open_list;
	size_t m_state_buf_size;

	// from test base
	CLfsInterface* m_fs;
	std::wstring m_root;	// 测试的根目录。所有测试再次目录下进行

	// 用于监控文件操作是否超时
	long m_running;
	DWORD m_message_interval = 30;
	HANDLE m_monitor_thread;
	HANDLE m_monitor_event;

	HANDLE m_fsinfo_file = nullptr;		// 文件系统的特殊文件，用于读取file system的一些状态


protected:
	// 测试汇总结果

	// configurations
protected:
*/
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== help functions ====

#define TEST_LOG_SINGLE(...)  {\
	swprintf_s(m_log_buf, __VA_ARGS__);	\
	LOG_DEBUG(m_log_buf);	\
	if (m_log_file) {fwprintf_s(m_log_file, L"%s\n", m_log_buf); \
	fflush(m_log_file);} }

#define TEST_LOG(...)  {\
	swprintf_s(m_log_buf, __VA_ARGS__);	\
	LOG_DEBUG(m_log_buf);	\
	if (m_log_file) {fwprintf_s(m_log_file, m_log_buf); \
	}}

#define TEST_ERROR(...) {	\
	swprintf_s(m_log_buf, __VA_ARGS__); \
	if (m_log_file) {fwprintf_s(m_log_file, L"[err] %s\n", m_log_buf); fflush(m_log_file);}\
	THROW_ERROR(ERR_USER, m_log_buf);	}

#define TEST_CLOSE_LOG {\
	if (m_log_file) {fwprintf_s(m_log_file, L"\n"); \
	fflush(m_log_file); }}

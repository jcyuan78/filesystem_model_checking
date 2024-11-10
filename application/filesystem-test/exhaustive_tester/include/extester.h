///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_simulator.h"
#include "reference_fs.h"
#include <list>
#include <boost/unordered_set.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



class CFsState
{
public:
	CFsState(void) {};
	~CFsState(void);
public:
	CReferenceFs m_ref_fs;
	IFsSimulator* m_real_fs = nullptr;
	TRACE_ENTRY m_op;		// 上一个操作如何执行到这一步
	CFsState* m_parent = nullptr;
	UINT m_ref=0;			// 被参考的子节点数量。
	int m_depth;			// 搜索深度

public:
	void Initialize(const std::string& root_path, IFsSimulator * fs)
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
	CFsState* m_free_list=nullptr;		// 被回收的 note 存放在这里
	size_t m_free_nr=0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == State heap and hash code
#define CODE_SIZE (MAX_ENCODE_SIZE / 4)
//#define CODE_SIZE (20)
#if 0
struct ENCODE
{
	DWORD code[CODE_SIZE];
	bool operator == (const ENCODE& e) const {
		return memcmp(code, e.code, CODE_SIZE) == 0;
	}
};
#else
struct ENCODE
{
	char code[MAX_ENCODE_SIZE];
	bool operator == (const ENCODE& e) const {
		return memcmp(code, e.code, MAX_ENCODE_SIZE) == 0;
	}
};
#endif

inline size_t hash_value(const ENCODE& e)
{
	size_t seed = 0;
	for (int ii = 0; ii < CODE_SIZE; ++ii)	boost::hash_combine(seed, e.code[ii]);
	return seed;
}

class CStateHeap
{
public:
	//比较state是否已经被检查过，如果是，则返回true，否者添加并返回false；
	bool Check(const CFsState* state);
	void Insert(const CFsState* state);
	size_t StateNr(void) const {
		return m_fs_state.size();
	}
	size_t size(void) { return m_fs_state.size(); }
protected:
	boost::unordered_set<ENCODE> m_fs_state;
};

class CExTester;

#define MAX_WORK_NR		64
//#define MAX_THREAD_NR	8

// 不同类型的多线程处理方式，只能定义一个
//#define SINGLE_THREAD
//#define MULTI_THREAD
// 线程队列处理：允许线程数量少于最大op数量
#define THREAD_QUEUE
// 线程池方式处理：预先准备足够的线程
//#define THREAD_POOL


struct WORK_CONTEXT
{
	CExTester* tester;
	CFsState* state;
	CFsState* src_state;
	ERROR_CODE ir;
	HANDLE hevent;
	HANDLE hstart;	//用于触发开始条件
	HANDLE hthread;	//线程句柄，用于自定义工作线程
	DWORD tid;
	PTP_WORK work_item;
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
	// 上层程序调用StopTest来强制终止目前测测试。
	virtual void StopTest(void);
	virtual void GetTestSummary(boost::property_tree::wptree& sum);

	// 多态化
protected:
	virtual int PreTest(void);
	virtual ERROR_CODE RunTest(void);
	virtual void FinishTest(void);

	// 公共方法
protected:
	// 根据现在的状态，枚举所有可能的操作，结果放入ops中
	size_t GenerateOps(CFsState* cur_state, std::vector<TRACE_ENTRY> &ops);

protected:
	virtual void ShowTestFailure(FILE* log);

protected:
	ERROR_CODE DoFsOperator(CFsState* cur_state, TRACE_ENTRY& op, std::list<CFsState*>::iterator& insert);
	ERROR_CODE FsOperatorCore(CFsState* state, TRACE_ENTRY& op);
	bool DoFsOperator_Pool(CFsState* cur_state, TRACE_ENTRY& op, WORK_CONTEXT * context);
	bool DoFsOperator_Thread(CFsState* cur_state, TRACE_ENTRY& op, WORK_CONTEXT* context);
	bool DoFsOperator_Queue(CFsState* cur_state, TRACE_ENTRY& op, WORK_CONTEXT* context);
	static VOID CALLBACK FsOperator_Callback(WORK_CONTEXT* context);
	static VOID CALLBACK FsOperator_Pool(PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_WORK work);
	static DWORD WINAPI FsOperator_Thread(PVOID context);
	static DWORD WINAPI _FsOperator_Queue(PVOID context)
	{
		CExTester* tester = (CExTester*)context;
		return tester->FsOperator_Queue();
	}
	DWORD FsOperator_Queue(void);

//	ERROR_CODE RunTrace(IFsSimulator* fs, const std::string& fn);


//	void FsOperator_Thread(work)

	ERROR_CODE TestCreateFile(CFsState* cur_state, const std::string& path);
	ERROR_CODE TestCreateDir (CFsState* cur_state, const std::string& path);
	ERROR_CODE TestWriteFile(CFsState * cur_state, const std::string& path, FSIZE offset, FSIZE len);
	ERROR_CODE TestWriteFileV2(CFsState * cur_state, NID fid, FSIZE offset, FSIZE len, const std::string &path);
	ERROR_CODE TestOpenFile(CFsState* cur_state, const std::string& path);
	ERROR_CODE TestCloseFile(CFsState* cur_state, NID fid, const std::string & path);
	ERROR_CODE TestDeleteFile(CFsState* cur_state, const std::string& path);
	ERROR_CODE TestDeleteDir(CFsState* cur_state, const std::string& path);
	ERROR_CODE TestMount(CFsState * cur_state);
	ERROR_CODE TestPowerOutage(CFsState* cur_state, UINT rollback);

//	int TestDelete(CFsState* state, CReferenceFs& ref, const std::wstring& path);
	int TestMove(CFsState* state, CReferenceFs& ref, const std::wstring& path_src, const std::wstring& path_dst);

	//	DWORD AppendChecksum(DWORD cur_checksum, const char* buf, size_t size);
	ERROR_CODE VerifyForPower(CFsState* cur_state);
	ERROR_CODE Verify(CFsState* cur_state);

	ERROR_CODE VerifyState(CReferenceFs& ref_fs, IFsSimulator* real_fs);

	// 针对每个子项，枚举所有可能的操作。
	//bool EnumerateOp(CFsState * cur_state, std::list<CFsState*>::iterator & insert);
	// 枚举子操作时，将Open和Write分开，实现可以同时打开多个文件。
	ERROR_CODE EnumerateOpV2(std::vector<TRACE_ENTRY>& ops, CFsState* cur_state, std::list<CFsState*>::iterator& insert);

	ERROR_CODE EnumerateOp_Thread(std::vector<TRACE_ENTRY>& ops, CFsState* cur_state, std::list<CFsState*>::iterator& insert);
	ERROR_CODE EnumerateOp_Thread_V2(std::vector<TRACE_ENTRY>& ops, CFsState* cur_state, 
		std::list<CFsState*>::iterator& insert);

	// for monitor thread
	static DWORD WINAPI _RunTest(PVOID p);
	bool PrintProgress(INT64 ts);
	bool OutputTrace(CFsState* state);
	bool OutputTrace_Thread(CFsState* state, ERROR_CODE ir, const std::string & err);
	void UpdateFsParam(IFsSimulator* fs);

protected:
	// make fs, mount fs, 和unmount fs是可选项。暂不支持。
	// make fs仅用于初始化。mount fs用于初始化和测试。测试中允许增加mount和unmount操作
	void TraceTestVerify(IFsSimulator* fs, const std::string &fn);

protected:
	// debug and monitor
	void ShowStack(CFsState* cur_state);

protected:
	CStateManager m_states;
	std::list<CFsState*> m_open_list;
	std::list<CFsState*> m_closed_list;		// 用于回溯找trace
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
	int m_branch;							// 统计测试时，抽选的分支数量

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
	// 正在扩展的状态，这个状态即不再open list也不在 closed list中。当遇到错误时，需要回收这个状态。
	CFsState* m_cur_state = nullptr;
	// 记录当前测试的最大深度，当测试终止时，会输出这个状态的trace。
	CFsState* m_max_depth_state = nullptr;
	// 控制测试停止，到该变量设置为1时，测试停止。
	LONG m_stop_test = 0;

	float m_max_memory_cost = 0;		// 最大内存使用量
	LONG m_max_depth = 0;

	// 文件系统性能（所有状态节点中最大的）
	FS_INFO m_max_fs_info;
	UINT m_total_item_num = 0, m_file_num=0;			// 总文件,目录数量 , 
	UINT m_logical_blks = 0, m_physical_blks = 0, m_total_blks, m_free_blks = -1;	// 逻辑饱和度，物理饱和度，空闲块
	LONG64 m_host_write=0, m_media_write=0;
	// 文件系统参数，用于限制测试范围
	UINT m_max_opened_file_nr;

	// log support
	std::wstring m_log_path;
	FILE* m_log_file = nullptr;
	char m_log_buf[1024];
	FILE* m_log_performance;

	// 其他
	IFsSimulator* m_fs_factory = nullptr;		// 这里的fs作为文件系统初始状态，以及的factory，用于创建文件测试用的文件系统。

	//用于多线程
	TP_CALLBACK_ENVIRON m_tp_environ;
	WORK_CONTEXT m_works[MAX_WORK_NR];
	HANDLE m_work_events[MAX_WORK_NR];
	UINT m_max_work = 0;

	std::list<WORK_CONTEXT*> m_sub_q;
	HANDLE m_sub_doorbell, m_cmp_doorbell;
	std::list<WORK_CONTEXT*> m_cmp_q;
	CRITICAL_SECTION m_sub_crit, m_cmp_crit;
	HANDLE *m_thread_list;
	UINT m_thread_num;	// 1: single thread
	CRITICAL_SECTION m_trace_crit;
};

class CExStatisticTester : public CExTester
{
public:
	CExStatisticTester(void);
	virtual ~CExStatisticTester(void);

protected:

	// 多态化
	virtual int PrepareTest(const boost::property_tree::wptree& config, IFsSimulator* fs, const std::wstring& log_path);
	virtual int PreTest(void);
	virtual ERROR_CODE RunTest(void);
	virtual void FinishTest(void);

protected:
	// 选择一个可执行的操作执行
	static DWORD WINAPI _OneTest(void);
	ERROR_CODE OneTest(void);

protected:
	int m_test_times;
	

};

class CExTraceTester : public CExTester
{
public:
	CExTraceTester(void) {};
	virtual ~CExTraceTester(void) {};

protected:

	// 多态化
	virtual int PrepareTest(const boost::property_tree::wptree& config, IFsSimulator* fs, const std::wstring& log_path);
	virtual int PreTest(void) { return 0; };
	virtual ERROR_CODE RunTest(void);
	virtual void FinishTest(void) {};

protected:
	//std::string m_trace_fn;
	boost::property_tree::ptree m_trace;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== help functions ====

#define TEST_LOG_SINGLE(...)  {\
	if (m_log_file) {fprintf_s(m_log_file, __VA_ARGS__); \
	fprintf_s(m_log_file, "\n"); \
	fflush(m_log_file);} }

#define TEST_LOG(...)  {\
	if (m_log_file) {fprintf_s(m_log_file, __VA_ARGS__); \
	}}

#define TEST_ERROR(...) {	\
	sprintf_s(m_log_buf, __VA_ARGS__); \
	if (m_log_file) {fprintf_s(m_log_file, "[err] ");\
		fprintf_s(m_log_file, __VA_ARGS__); \
		fprintf_s(m_log_file, "\n");\
	fflush(m_log_file);}\
	/*THROW_ERROR(ERR_USER, m_log_buf);*/	}

#define TEST_CLOSE_LOG {\
	if (m_log_file) {fprintf_s(m_log_file, "\n"); \
	fflush(m_log_file); }}


template <size_t N> void Op2String(char(&str)[N], TRACE_ENTRY& op);

// len: fn缓存长度。返回fn长度
int GenerateFn(char* fn, int len);
const char* OpName(OP_CODE op_code);


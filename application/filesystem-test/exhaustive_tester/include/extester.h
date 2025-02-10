///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_simulator.h"
#include "reference_fs.h"
#include <list>
#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define MAX_WORK_NR		512
//#define MAX_THREAD_NR	8

// 不同类型的多线程处理方式，只能定义一个
//#define SINGLE_THREAD
//#define MULTI_THREAD
// 线程队列处理：允许线程数量少于最大op数量
#define THREAD_QUEUE
// 线程池方式处理：预先准备足够的线程
//#define THREAD_POOL
//#define STATE_MANAGER_THREAD_SAFE


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
	UINT m_depth;			// 搜索深度
	bool		m_stable = false;	// true: 表示这是一个稳定状态。Power Loss的修复不能跨过这个状态。
	void add_ref() {	InterlockedIncrement(&m_ref);	}
	ERROR_CODE m_result;
protected:
	UINT m_ref=1;			// (被参考的子节)引用计数。
	friend class CStateManager;

public:
	void Initialize(const std::string& root_path, IFsSimulator * fs)
	{
		m_ref_fs.Initialize(root_path);
		m_real_fs = fs;
	}
	void OutputState(FILE* log_file);
	// 复制ref fs和real fs
	void DuplicateFrom(CFsState* src_state);
	// 仅复制ref fs, real fs共用一个实体。
	void DuplicateWithoutFs(CFsState* src_state);
};

class CStateManager
{
public:
	CStateManager(void);
	~CStateManager(void);
	
public:
	void Initialize(size_t size, bool duplicate_real_fs = true);
	CFsState* get(void);
	void put(CFsState* &state);
	CFsState* duplicate(CFsState* state);

protected:
	CFsState* m_free_list=nullptr;		// 被回收的 note 存放在这里
	// 在复制状态的时候，是否要复制real fs
	bool m_duplicate_real_fs;
//#ifdef STATE_MANAGER_THREAD_SAFE
	CRITICAL_SECTION m_lock;
//#endif
public:
	size_t m_free_nr=0;
	// for debug
	//size_t m_buffer_size = 0;
	//CFsState* states[500];
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
	bool CheckAndInsert(const CFsState* state);
	size_t StateNr(void) const {
		return m_fs_state.size();
	}
	size_t size(void) { return m_fs_state.size(); }
protected:
//	boost::unordered_set<ENCODE> m_fs_state;
	boost::unordered_map<ENCODE, int> m_fs_state;
};

class CExTester;




struct WORK_CONTEXT
{
	CExTester* tester;
	CFsState* state;
	CFsState* src_state;
	ERROR_CODE result;
	UINT test_id;
	UINT seed;
	HANDLE event_start;		// 用于出发开始条件
	HANDLE event_complete;	// 用于触发结束条件
#ifdef THREAD_POOL
	PTP_WORK work_item;
#endif
#ifdef MULTI_THREAD
	HANDLE hthread;	//线程句柄，用于自定义工作线程
	DWORD tid;
#endif
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
	//size_t GenerateOps(CFsState* cur_state, std::vector<TRACE_ENTRY> &ops);
	size_t GenerateOps(CFsState* cur_state, TRACE_ENTRY * ops, size_t op_size);

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

	ERROR_CODE TestCreateFile(CFsState* cur_state, const std::string& path);
	ERROR_CODE TestCreateDir (CFsState* cur_state, const std::string& path);
//	ERROR_CODE TestWriteFile(CFsState * cur_state, const std::string& path, FSIZE offset, FSIZE len);
	ERROR_CODE TestWriteFileV2(CFsState * cur_state, _NID fid, FSIZE offset, FSIZE len, const std::string &path);
	ERROR_CODE TestOpenFile(CFsState* cur_state, const std::string& path);
	ERROR_CODE TestCloseFile(CFsState* cur_state, _NID fid, const std::string & path);
	ERROR_CODE TestDeleteFile(CFsState* cur_state, const std::string& path);
	ERROR_CODE TestDeleteDir(CFsState* cur_state, const std::string& path);
	ERROR_CODE TestMount(CFsState * cur_state);
	ERROR_CODE TestPowerOutage(CFsState* cur_state, UINT rollback);

	int TestMove(CFsState* state, CReferenceFs& ref, const std::wstring& path_src, const std::wstring& path_dst);

	ERROR_CODE VerifyForPower(CFsState* cur_state);
	ERROR_CODE Verify(CFsState* cur_state);

	ERROR_CODE VerifyState(CReferenceFs& ref_fs, IFsSimulator* real_fs);

	// 针对每个子项，枚举所有可能的操作。
	//bool EnumerateOp(CFsState * cur_state, std::list<CFsState*>::iterator & insert);
	// 枚举子操作时，将Open和Write分开，实现可以同时打开多个文件。
	ERROR_CODE EnumerateOpV2(TRACE_ENTRY * ops, size_t op_size, CFsState* cur_state, std::list<CFsState*>::iterator& insert);

	ERROR_CODE EnumerateOp_Thread(TRACE_ENTRY* ops, size_t op_size, CFsState* cur_state, std::list<CFsState*>::iterator& insert);
	ERROR_CODE EnumerateOp_Thread_V2(TRACE_ENTRY* ops, size_t op_size, CFsState* cur_state,
		std::list<CFsState*>::iterator& insert);


	enum TRACE_OPTION {
		TRACE_FILES	= 0x00000001,	TRACE_REAL_FS	= 0x00000002,	TRACE_GC	= 0x00000004,
		TRACE_ENCODE= 0x00000008,	TRACE_REF_FS	= 0x00000010,	TRACE_JSON	= 0x00000020,
		TRACE_SUMMARY=0x00000040,
	};
	// for monitor thread
	static DWORD WINAPI _RunTest(PVOID p);
	virtual bool PrintProgress(INT64 ts);
	bool OutputTrace(CFsState* state);
	bool OutputTrace_Thread(CFsState* state, ERROR_CODE ir, const std::string & err, DWORD trace_id=0, DWORD option=0);
	bool OutputTrace(FILE* out_file, const std::string & json_fn, CFsState* state, DWORD option);
	void UpdateFsParam(IFsSimulator* fs);
	void RealFsState(FILE* out_file, IFsSimulator* real_fs, bool file_nr );

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
	bool m_check_power_loss;				

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

	// log support
	std::wstring m_log_path;
	FILE* m_log_file = nullptr;
	char m_log_buf[1024];
	FILE* m_log_performance;
	UINT m_error_list[ERR_ERROR_NR];	// 用于记录不同error code发生的深度，保存每个error code最下深度的trace。

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
	virtual bool PrintProgress(INT64 ts);
	virtual void GetTestSummary(boost::property_tree::wptree& sum);

protected:
	// 选择一个可执行的操作执行
//	static DWORD WINAPI _OneTest(PTP_CALLBACK_ENVIRON instance, PVOID context, PTP_WORK work);

	DWORD RunTestQueue(WORK_CONTEXT * work);

	static DWORD WINAPI _RunTestQueue(PVOID context)
	{
//		CExStatisticTester* tester = (CExStatisticTester*)context;
//		return tester->RunTestQueue();
		WORK_CONTEXT* work = (WORK_CONTEXT*)context;
		CExStatisticTester* tester = dynamic_cast<CExStatisticTester*>(work->tester);
		return tester->RunTestQueue(work);
	}
	ERROR_CODE OneTest(CFsState* init_state, DWORD test_id, int seed, TRACE_ENTRY * op_buf, CStateManager * states = nullptr);

protected:
	LONG m_test_times;
	LONG m_tested, m_failed, m_testing;
	

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


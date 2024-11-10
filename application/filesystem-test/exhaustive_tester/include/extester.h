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
	TRACE_ENTRY m_op;		// ��һ���������ִ�е���һ��
	CFsState* m_parent = nullptr;
	UINT m_ref=0;			// ���ο����ӽڵ�������
	int m_depth;			// �������

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
	CFsState* m_free_list=nullptr;		// �����յ� note ���������
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
	//�Ƚ�state�Ƿ��Ѿ�������������ǣ��򷵻�true��������Ӳ�����false��
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

// ��ͬ���͵Ķ��̴߳���ʽ��ֻ�ܶ���һ��
//#define SINGLE_THREAD
//#define MULTI_THREAD
// �̶߳��д��������߳������������op����
#define THREAD_QUEUE
// �̳߳ط�ʽ����Ԥ��׼���㹻���߳�
//#define THREAD_POOL


struct WORK_CONTEXT
{
	CExTester* tester;
	CFsState* state;
	CFsState* src_state;
	ERROR_CODE ir;
	HANDLE hevent;
	HANDLE hstart;	//���ڴ�����ʼ����
	HANDLE hthread;	//�߳̾���������Զ��幤���߳�
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
	// �ϲ�������StopTest��ǿ����ֹĿǰ����ԡ�
	virtual void StopTest(void);
	virtual void GetTestSummary(boost::property_tree::wptree& sum);

	// ��̬��
protected:
	virtual int PreTest(void);
	virtual ERROR_CODE RunTest(void);
	virtual void FinishTest(void);

	// ��������
protected:
	// �������ڵ�״̬��ö�����п��ܵĲ������������ops��
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

	// ���ÿ�����ö�����п��ܵĲ�����
	//bool EnumerateOp(CFsState * cur_state, std::list<CFsState*>::iterator & insert);
	// ö���Ӳ���ʱ����Open��Write�ֿ���ʵ�ֿ���ͬʱ�򿪶���ļ���
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
	// make fs, mount fs, ��unmount fs�ǿ�ѡ��ݲ�֧�֡�
	// make fs�����ڳ�ʼ����mount fs���ڳ�ʼ���Ͳ��ԡ���������������mount��unmount����
	void TraceTestVerify(IFsSimulator* fs, const std::string &fn);

protected:
	// debug and monitor
	void ShowStack(CFsState* cur_state);

protected:
	CStateManager m_states;
	std::list<CFsState*> m_open_list;
	std::list<CFsState*> m_closed_list;		// ���ڻ�����trace
	CStateHeap m_closed;

protected:
	// ���Բ����������ȣ�����ļ�����
	DWORD m_update_ms = 50000;		// log/��Ļ����ʱ�䣨���뵥λ��
	size_t m_max_child_num;					// Ŀ¼�µ�������ļ�/��Ŀ¼����
	size_t m_max_dir_depth;					// ���Ŀ¼���
	size_t m_max_file_op;					// ����ļ�������д�룩����
	FSIZE m_max_file_size;					// �ļ������ߴ�
	int m_clear_temp;						// �������Ժ��Ƿ����������ʱ�ļ�
	int m_test_depth;						// ���������
	size_t m_volume_size;					// �ļ�ϵͳ��С
	int m_branch;							// ͳ�Ʋ���ʱ����ѡ�ķ�֧����

	std::vector<OP_CODE> m_file_op_set;		// �����ļ�������Ĳ���
	std::vector<OP_CODE> m_dir_op_set;		// ����Ŀ¼������Ĳ���
	std::vector<OP_CODE> m_fs_op_set;			// �����ļ�ϵͳ������Ĳ���������unmount/mount. spor��
	// ��������
	bool m_mount, m_format;		// �ڲ���ǰ�Ƿ�Ҫformat��mount fs
	DWORD m_timeout;

	// ��������״̬
	UINT m_op_sn;
	boost::posix_time::ptime m_ts_start;
	DWORD m_test_thread_id;
	HANDLE m_test_thread;
	HANDLE m_test_event;
	// ������չ��״̬�����״̬������open listҲ���� closed list�С�����������ʱ����Ҫ�������״̬��
	CFsState* m_cur_state = nullptr;
	// ��¼��ǰ���Ե������ȣ���������ֹʱ����������״̬��trace��
	CFsState* m_max_depth_state = nullptr;
	// ���Ʋ���ֹͣ�����ñ�������Ϊ1ʱ������ֹͣ��
	LONG m_stop_test = 0;

	float m_max_memory_cost = 0;		// ����ڴ�ʹ����
	LONG m_max_depth = 0;

	// �ļ�ϵͳ���ܣ�����״̬�ڵ������ģ�
	FS_INFO m_max_fs_info;
	UINT m_total_item_num = 0, m_file_num=0;			// ���ļ�,Ŀ¼���� , 
	UINT m_logical_blks = 0, m_physical_blks = 0, m_total_blks, m_free_blks = -1;	// �߼����Ͷȣ������Ͷȣ����п�
	LONG64 m_host_write=0, m_media_write=0;
	// �ļ�ϵͳ�������������Ʋ��Է�Χ
	UINT m_max_opened_file_nr;

	// log support
	std::wstring m_log_path;
	FILE* m_log_file = nullptr;
	char m_log_buf[1024];
	FILE* m_log_performance;

	// ����
	IFsSimulator* m_fs_factory = nullptr;		// �����fs��Ϊ�ļ�ϵͳ��ʼ״̬���Լ���factory�����ڴ����ļ������õ��ļ�ϵͳ��

	//���ڶ��߳�
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

	// ��̬��
	virtual int PrepareTest(const boost::property_tree::wptree& config, IFsSimulator* fs, const std::wstring& log_path);
	virtual int PreTest(void);
	virtual ERROR_CODE RunTest(void);
	virtual void FinishTest(void);

protected:
	// ѡ��һ����ִ�еĲ���ִ��
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

	// ��̬��
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

// len: fn���泤�ȡ�����fn����
int GenerateFn(char* fn, int len);
const char* OpName(OP_CODE op_code);


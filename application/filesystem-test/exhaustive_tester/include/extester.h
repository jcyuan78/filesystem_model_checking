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
	OK_ALREADY_EXIST,	// �ļ�����Ŀ¼�Ѿ����ڣ����ǽ������true
	ERR_CREATE_EXIST,	// �����Ѿ����ڵ��ļ����ظ�������
	ERR_CREATE,			// �ļ�����Ŀ¼�����ڣ����Ǵ���ʧ��
	ERR_OPEN_FILE,		// ��ͼ��һ���Ѿ����ڵ��ļ��ǳ���
	ERR_GET_INFOMATION,	// ��ȡFile Informaitonʱ����
	ERR_DELETE_FILE,	// ɾ���ļ�ʱ����
	ERR_DELETE_DIR,		// ɾ��Ŀ¼ʱ����
	ERR_READ_FILE,		// ���ļ�ʱ����
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
	TRACE_ENTRY m_op;		// ��һ���������ִ�е���һ��
	int m_depth;			// �������

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
	//�Ƚ�state�Ƿ��Ѿ�������������ǣ��򷵻�true��������Ӳ�����false��
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
	// ����һ��ִ�к���µ�״̬
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

	// ���ÿ�����ö�����п��ܵĲ�����
	bool EnumerateOp(CFsState * cur_state, std::list<CFsState*>::iterator & insert);

	// for monitor thread
	//DWORD Monitor(void);
	static DWORD WINAPI _RunTest(PVOID p);
	bool PrintProgress(INT64 ts);

protected:


protected:
	// make fs, mount fs, ��unmount fs�ǿ�ѡ��ݲ�֧�֡�
	// make fs�����ڳ�ʼ����mount fs���ڳ�ʼ���Ͳ��ԡ���������������mount��unmount����
	int MakeFs(void) { return 0; }
	int MountFs(void) { return 0; }
	int UnmountFs(void) { return 0; }

protected:
	// debug and monitor
	void ShowStack(CFsState* cur_state);

protected:
	// state �ڴ������
	//void InitStateBuf(size_t state_nr);
	//CFsState* get_state(void);
	//void put_state(CFsState* state);

protected:
	CStateManager m_states;
	std::list<CFsState*> m_open_list;
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

	float m_max_memory_cost = 0;		// ����ڴ�ʹ����
	LONG m_max_depth = 0;


	// log support
	std::wstring m_log_path;
	FILE* m_log_file = nullptr;
	wchar_t m_log_buf[1024];


	// ����
	IFsSimulator* m_fs_factory = nullptr;		// �����fs��Ϊ�ļ�ϵͳ��ʼ״̬���Լ���factory�����ڴ����ļ������õ��ļ�ϵͳ��

/*
	// ������ѡ��
protected:
	//	bool m_need_mount = false;
	bool m_support_trunk = true;


	CStateHeap m_states;
	size_t m_state_searched = 0;
	size_t m_max_depth = 0;

protected:
	int m_cur_depth;						// ��ǰ���Ե����

	CFsState* m_test_state_buf;
	std::list<CFsState*> m_free_list;
	std::list<CFsState*> m_open_list;
	size_t m_state_buf_size;

	// from test base
	CLfsInterface* m_fs;
	std::wstring m_root;	// ���Եĸ�Ŀ¼�����в����ٴ�Ŀ¼�½���

	// ���ڼ���ļ������Ƿ�ʱ
	long m_running;
	DWORD m_message_interval = 30;
	HANDLE m_monitor_thread;
	HANDLE m_monitor_event;

	HANDLE m_fsinfo_file = nullptr;		// �ļ�ϵͳ�������ļ������ڶ�ȡfile system��һЩ״̬


protected:
	// ���Ի��ܽ��

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

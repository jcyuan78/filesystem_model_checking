///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "reference_fs.h"
#include "tester_base.h"
#include <vector>
#include <boost/property_tree/json_parser.hpp>



typedef TRACE_ENTRY* PFSOP;

// 一个原子操作的队列，带有door bell
template <typename T>
class CAtomicVector
{
public:
	CAtomicVector(void) { InitializeSRWLock(&m_lock); }
public:
	void push_back_s(const T obj)
	{
		AcquireSRWLockExclusive(&m_lock);
		m_vector.push_back(obj);
		ReleaseSRWLockExclusive(&m_lock);
	}
	size_t size_s()
	{
		AcquireSRWLockShared(&m_lock);
		size_t ss = m_vector.size();
		ReleaseSRWLockShared(&m_lock);
		return ss;
	}
	T& at_s(size_t index)
	{
		AcquireSRWLockShared(&m_lock);
		T& t = m_vector.at(index);
		ReleaseSRWLockShared(&m_lock);
		return t;
	}

protected:
	std::vector<T> m_vector;
	SRWLOCK m_lock;
};

class CTestState
{
public:
	//TRACE_ENTRY m_op;	// 通过什么操作获得此状态
	CReferenceFs m_ref_fs;
	std::vector<TRACE_ENTRY> m_ops;		// 将要执行的操作
	UINT32 m_cur_op = 0;

	CAtomicVector<TRACE_ENTRY> m_history;	// 经过那些操作到达这一步的，多线程下可能由多个操作导致新的状态。

public:
	void Initialize(const std::wstring & root_path);
	void Initialize(const CReferenceFs* src);
	//void AddOperation(OP_CODE op_id, const std::wstring& src_path, const std::wstring& param1, UINT64 param3=0, UINT64 param4=0);
	void AddCreateOperation(const std::wstring& src_path, const std::wstring tar_path, bool is_dir);
	void AddWriteOperation(const std::wstring& src_path, UINT64 offset, UINT64 length);

//	bool EnumerateOp(bool test_spor);
	void OutputState(FILE* log_file);
protected:
	void GenerateFn(wchar_t* fn, size_t len);
};


enum CONDITION_MASK
{
	COND_FILE = 0x001,
	COND_DIR = 0x002,
	COND_FS = 0x004,
};

class OperatorDescription
{
public:
	OP_CODE m_op_id;		// 操作码
	UINT32 m_cond;	// 选择码，那些条件适合使用次次操作
};

class IFileSystemAdaptor
{

};


class CFullTester : public CTesterBase
{
public:
	CFullTester(IFileSystem* fs, IVirtualDisk* disk) : CTesterBase(fs, disk) {}
public:
//	int StartTest(void);
	virtual void Config(const boost::property_tree::wptree& pt, const std::wstring& root);

	void SetTestRoot(const std::wstring& root) { m_root = root; }

protected:
	virtual int PrepareTest(void);
	virtual int RunTest(void);
	virtual int FinishTest(void);
	virtual void ShowTestFailure(FILE* log);

	// 执行文件操作
	int FsOperate(CReferenceFs& ref, TRACE_ENTRY* op);
//	int TestCreate(CReferenceFs& ref, const std::wstring & path, const std::wstring & fn, bool isdir);
	int TestCreate(CReferenceFs& ref, const std::wstring & path, bool isdir);
//	int TestMount(IFileSystem* fs, CReferenceFs& ref);
	int TestWrite(CReferenceFs& ref, /*bool overwrite,*/ const std::wstring& path, size_t offset, size_t len);
//	bool TestPower(IFileSystem* fs, CReferenceFs& ref);


	int Rollback(CReferenceFs& ref, const TRACE_ENTRY* op);
//	DWORD AppendChecksum(DWORD cur_checksum, const char* buf, size_t size);
	int Verify(const CReferenceFs& ref);

	// 针对每个子项，枚举所有可能的操作。
	bool EnumerateOp(CTestState & state);



protected:
	// make fs, mount fs, 和unmount fs是可选项。暂不支持。
	// make fs仅用于初始化。mount fs用于初始化和测试。测试中允许增加mount和unmount操作
	int MakeFs(void) { return 0; }
	int MountFs(void) { return 0; }
	int UnmountFs(void) { return 0; }

	// 参数，选项
protected:
	bool m_need_mount = false;
	bool m_support_trunk = true;
	int m_test_depth;	// 最大测试深度
	std::vector<OP_CODE> m_file_op_set;		// 对于文件，允许的操作
	std::vector<OP_CODE> m_dir_op_set;		// 对于目录，允许的操作
	std::vector<OP_CODE> m_fs_op_set;			// 对于文件系统，允许的操作，例如unmount/mount. spor等

	size_t m_max_child_num;					// 目录下的最大子文件/子目录数量
	size_t m_max_file_size;					// 文件的最大尺寸
	int m_clear_temp;						// 测试完以后是否清除所有临时文件

protected:
	CTestState	m_test_state[MAX_DEPTH + 10];
	int m_cur_depth;						// 当前测试的深度
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
	/*fflush(m_log_file);*/ }}

#define TEST_ERROR(...) {	\
	swprintf_s(m_log_buf, __VA_ARGS__); \
	if (m_log_file) {fwprintf_s(m_log_file, L"[err] %s\n", m_log_buf); fflush(m_log_file);}\
	THROW_ERROR(ERR_USER, m_log_buf);	}

#define TEST_CLOSE_LOG {\
	if (m_log_file) {fwprintf_s(m_log_file, L"\n"); \
	fflush(m_log_file); }}

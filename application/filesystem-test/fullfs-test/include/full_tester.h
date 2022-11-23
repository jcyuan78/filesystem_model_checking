///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "reference_fs.h"
#include <vector>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/property_tree/json_parser.hpp>


enum class OP_ID
{						// action,		P1,				P2,	P3, P4
	OP_NONE =0,			
	OP_NO_EFFECT,		//对于不产生影响的操作，不进行回滚
	CREATE_FILE,		//创建文件,		新文件名,		na
	CREATE_DIR,			//创建目录,		新目录名,		na
	DELETE_FILE,		//
	DELETE_DIR,
	MOVE,				//移动或改名,	目标路径、文件名,	na
	APPEND_FILE,		
	OVER_WRITE,			//写文件,		na,				na,	偏移量,	长度
	DEMOUNT_MOUNT,
	POWER_OFF_RECOVERY,
};

enum ERROR_CODE
{
	ERR_OK = 0,
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


class FS_OP
{
public:
	FS_OP(void) : op_id(OP_ID::OP_NONE) {};
	FS_OP(OP_ID id, const std::wstring& fn) : op_id(id), path(fn) {};
	FS_OP(OP_ID id, const std::wstring& fn, const std::wstring& p1) : op_id(id), path(fn), param1_str(p1) {};
public:
	OP_ID op_id;
	std::wstring path;
	std::wstring param1_str;
	//std::wstring param2_str;
	UINT64 param3_val;			// 用于写操作
	UINT64 param4_val;			// 
	UINT op_sn;		// 操作的序列号

};

typedef FS_OP* PFSOP;

class CTestState
{
public:
	//FS_OP m_op;	// 通过什么操作获得此状态
	CReferenceFs m_ref_fs;
	std::vector<FS_OP> m_ops;
	UINT32 m_cur_op = 0;

public:
	void Initialize(const std::wstring & root_path);
	void Initialize(const CReferenceFs* src);
	void AddOperation(OP_ID op_id, const std::wstring& src_path, const std::wstring& param1, UINT64 param3=0, UINT64 param4=0);
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
	OP_ID m_op_id;		// 操作码
	UINT32 m_cond;	// 选择码，那些条件适合使用次次操作

};

class CFullTester
{
public:

	int StartTest(void);
	void Config(const boost::property_tree::wptree& pt);
	void SetLogFile(const std::wstring & log_fn);

	void SetTestRoot(const std::wstring& root) { m_root = root; }

protected:
	int PrepareTest(void);
	int RunTest(void);
	int FinishTest(void);

	// 执行文件操作
	int FsOperate(CReferenceFs& ref, FS_OP* op);
	int TestCreate(CReferenceFs& ref, const std::wstring & path, const std::wstring & fn, bool isdir);
//	int TestMount(IFileSystem* fs, CReferenceFs& ref);
	int TestWrite(CReferenceFs& ref, /*bool overwrite,*/ const std::wstring& path, size_t offset, size_t len);
//	bool TestPower(IFileSystem* fs, CReferenceFs& ref);


	int Rollback(CReferenceFs& ref, const FS_OP* op);
//	DWORD AppendChecksum(DWORD cur_checksum, const char* buf, size_t size);
	int Verify(const CReferenceFs& ref);
	bool PrintProgress(INT64 ts);

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
	std::wstring m_root;	// 测试的根目录。所有测试再次目录下进行
	int m_test_depth;	// 最大测试深度
	std::vector<OP_ID> m_file_op_set;		// 对于文件，允许的操作
	std::vector<OP_ID> m_dir_op_set;		// 对于目录，允许的操作
	std::vector<OP_ID> m_fs_op_set;			// 对于文件系统，允许的操作，例如unmount/mount. spor等

	size_t m_max_child_num;					// 目录下的最大子文件/子目录数量
	size_t m_max_file_size;					// 文件的最大尺寸
	int m_clear_temp;						// 测试完以后是否清除所有临时文件

protected:
	CTestState	m_test_state[MAX_DEPTH + 10];
	FILE * m_log_file = nullptr;
	wchar_t m_log_buf[1024];
	boost::posix_time::ptime m_ts_start;
	UINT m_op_sn;
	int m_cur_depth;						// 当前测试的深度

	// 用于监控文件操作测超市
	long m_running;
	DWORD m_timeout;
	DWORD m_message_interval=30;
	HANDLE m_monitor_thread;
	HANDLE m_monitor_event;
	DWORD Monitor(void);
	static DWORD WINAPI _Monitor(PVOID p)
	{
		CFullTester* tester = (CFullTester*)p;
		return tester->Monitor();
	}

};